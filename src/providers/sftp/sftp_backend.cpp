#include "providers/sftp_backend.hpp"
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <sstream>

namespace duckdb {

// ─── libssh2 one-time init ────────────────────────────────────────────────────
struct Libssh2Init {
    Libssh2Init()  { libssh2_init(0); }
    ~Libssh2Init() { libssh2_exit(); }
};
static Libssh2Init g_ssh2_init;

// ─── SSHConnection::Close ─────────────────────────────────────────────────────
void SSHConnection::Close() {
    if (sftp)    { libssh2_sftp_shutdown(sftp);    sftp    = nullptr; }
    if (session) { libssh2_session_disconnect(session, "Bye");
                   libssh2_session_free(session);  session = nullptr; }
    if (socket_fd >= 0) { ::close(socket_fd); socket_fd = -1; }
}

SFTPBackend::~SFTPBackend() {
    for (auto &[k, c] : connections_) c.Close();
}

// ─── URL parsing ──────────────────────────────────────────────────────────────
// sftp://user@host:port/path
bool SFTPBackend::ParseUrl(const std::string &url,
                            std::string &out_root, std::string &out_path,
                            std::string &err) const {
    const std::string pfx = "sftp://";
    if (url.substr(0, pfx.size()) != pfx) { err = "not sftp://"; return false; }
    std::string rest = url.substr(pfx.size());

    // Find first '/' that separates authority from path
    auto slash = rest.find('/');
    out_root = (slash == std::string::npos) ? rest        : rest.substr(0, slash);
    out_path = (slash == std::string::npos) ? "/"         : rest.substr(slash);
    return true;
}

// ─── Connection management ────────────────────────────────────────────────────
// Parse root: "user@host" or "user@host:port"
static bool ParseRoot(const std::string &root,
                       std::string &out_user, std::string &out_host, int &out_port,
                       std::string &err) {
    out_port = 22;
    auto at = root.rfind('@');
    if (at == std::string::npos) { err = "sftp root must be user@host[:port]"; return false; }
    out_user = root.substr(0, at);
    std::string hostport = root.substr(at + 1);
    auto colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        out_host = hostport.substr(0, colon);
        out_port = std::stoi(hostport.substr(colon + 1));
    } else {
        out_host = hostport;
    }
    return true;
}

static int ConnectSocket(const std::string &host, int port, std::string &err) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        err = "DNS resolution failed for: " + host;
        return -1;
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0 || ::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        err = "TCP connect failed to " + host + ":" + port_str;
        return -1;
    }
    freeaddrinfo(res);
    return fd;
}

SSHConnection &SFTPBackend::GetConnection(const std::string &root,
                                           const std::string &token,
                                           std::string &err) {
    std::lock_guard<std::mutex> lk(conn_mutex_);
    auto it = connections_.find(root);
    if (it != connections_.end() && it->second.IsValid())
        return it->second;

    // Close stale if exists
    if (it != connections_.end()) it->second.Close();

    // Parse root
    std::string user, host; int port;
    if (!ParseRoot(root, user, host, port, err)) {
        static SSHConnection bad;
        return bad;
    }

    SSHConnection conn;
    conn.socket_fd = ConnectSocket(host, port, err);
    if (conn.socket_fd < 0) { static SSHConnection bad; return bad; }

    conn.session = libssh2_session_init();
    if (!conn.session) { err = "libssh2_session_init failed"; ::close(conn.socket_fd); static SSHConnection bad; return bad; }

    libssh2_session_set_blocking(conn.session, 1);
    if (libssh2_session_handshake(conn.session, conn.socket_fd) != 0) {
        char *msg; libssh2_session_last_error(conn.session, &msg, nullptr, 0);
        err = "SSH handshake failed: " + std::string(msg);
        conn.Close(); static SSHConnection bad; return bad;
    }

    // Authenticate
    // token format: "keyfile:/path/to/key[:passphrase]" | "agent:" | "password:pw"
    int rc = -1;
    if (token.substr(0, 8) == "keyfile:") {
        std::string rest  = token.substr(8);
        auto colon        = rest.find(':');
        std::string keyfile    = (colon == std::string::npos) ? rest : rest.substr(0, colon);
        std::string passphrase = (colon == std::string::npos) ? ""   : rest.substr(colon + 1);
        // derive public key path: keyfile + ".pub"
        std::string pubkey = keyfile + ".pub";
        rc = libssh2_userauth_publickey_fromfile(conn.session,
                user.c_str(), pubkey.c_str(), keyfile.c_str(),
                passphrase.empty() ? nullptr : passphrase.c_str());
    } else if (token.substr(0, 6) == "agent:") {
        LIBSSH2_AGENT *agent = libssh2_agent_init(conn.session);
        libssh2_agent_connect(agent);
        libssh2_agent_list_identities(agent);
        struct libssh2_agent_publickey *identity = nullptr, *prev = nullptr;
        while (!libssh2_agent_get_identity(agent, &identity, prev)) {
            if (libssh2_agent_userauth(agent, user.c_str(), identity) == 0) { rc = 0; break; }
            prev = identity;
        }
        libssh2_agent_free(agent);
    } else if (token.substr(0, 9) == "password:") {
        std::string pw = token.substr(9);
        rc = libssh2_userauth_password(conn.session, user.c_str(), pw.c_str());
    }

    if (rc != 0) {
        char *msg; libssh2_session_last_error(conn.session, &msg, nullptr, 0);
        err = "SSH auth failed: " + std::string(msg);
        conn.Close(); static SSHConnection bad; return bad;
    }

    conn.sftp = libssh2_sftp_init(conn.session);
    if (!conn.sftp) {
        err = "SFTP init failed";
        conn.Close(); static SSHConnection bad; return bad;
    }

    connections_[root] = std::move(conn);
    return connections_[root];
}

// ─── Stat ─────────────────────────────────────────────────────────────────────
bool SFTPBackend::Stat(const std::string &root, const std::string &path,
                        const std::string &token, CloudItem &out, std::string &err) {
    auto &conn = GetConnection(root, token, err);
    if (!conn.IsValid()) return false;

    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    if (libssh2_sftp_stat(conn.sftp, path.c_str(), &attrs) != 0) {
        err = "sftp stat failed: " + path; return false;
    }
    out.id        = path;    // SFTP uses paths as IDs
    out.path      = path;
    out.name      = path.substr(path.rfind('/') + 1);
    out.size      = (int64_t)attrs.filesize;
    out.is_folder = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
    out.modified_time_ms = (int64_t)attrs.mtime * 1000;
    return true;
}

// ─── ReadRange ────────────────────────────────────────────────────────────────
int64_t SFTPBackend::ReadRange(const CloudItem &item, const std::string &root,
                                const std::string &token,
                                int64_t off, int64_t len, char *buf, std::string &err) {
    auto &conn = GetConnection(root, token, err);
    if (!conn.IsValid()) return -1;

    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(conn.sftp, item.path.c_str(),
                                                  LIBSSH2_FXF_READ, 0);
    if (!fh) { err = "sftp open failed: " + item.path; return -1; }

    libssh2_sftp_seek64(fh, (libssh2_uint64_t)off);

    int64_t total_read = 0;
    while (total_read < len) {
        int n = libssh2_sftp_read(fh, buf + total_read, (size_t)(len - total_read));
        if (n == 0) break;       // EOF
        if (n  < 0) { err = "sftp read error"; libssh2_sftp_close(fh); return -1; }
        total_read += n;
    }
    libssh2_sftp_close(fh);
    return total_read;
}

// ─── ListFolder ───────────────────────────────────────────────────────────────
bool SFTPBackend::ListFolder(const std::string &root, const std::string &folder_id,
                              const std::string &token,
                              const std::function<void(const CloudItem &)> &cb,
                              std::string &cursor, std::string &err) {
    if (!cursor.empty()) { cursor.clear(); return true; } // SFTP has no pagination

    auto &conn = GetConnection(root, token, err);
    if (!conn.IsValid()) return false;

    LIBSSH2_SFTP_HANDLE *dh = libssh2_sftp_opendir(conn.sftp, folder_id.c_str());
    if (!dh) { err = "sftp opendir failed: " + folder_id; return false; }

    char name_buf[512];
    char longentry[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs{};

    while (true) {
        int rc = libssh2_sftp_readdir_ex(dh, name_buf, sizeof(name_buf),
                                          longentry, sizeof(longentry), &attrs);
        if (rc <= 0) break;
        std::string name(name_buf);
        if (name == "." || name == "..") continue;

        CloudItem c;
        c.name      = name;
        c.path      = folder_id + (folder_id.back() == '/' ? "" : "/") + name;
        c.id        = c.path;
        c.size      = (int64_t)attrs.filesize;
        c.is_folder = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        c.modified_time_ms = (int64_t)attrs.mtime * 1000;
        cb(c);
    }
    libssh2_sftp_closedir(dh);
    cursor.clear();
    return true;
}

// ─── Write (streaming, no session needed) ────────────────────────────────────
bool SFTPBackend::CreateUploadSession(const std::string &root,
                                       const std::string &parent_id,
                                       const std::string &name, int64_t,
                                       const std::string &token,
                                       CloudUploadSession &out, std::string &err) {
    // Encode destination path in upload_url field
    out.upload_url        = parent_id + "/" + name;
    out.chunk_size_bytes  = 4 * 1024 * 1024; // 4 MiB
    out.total_size_bytes  = -1;
    return true;
}

bool SFTPBackend::UploadChunk(const CloudUploadSession &s, const char *data,
                               int64_t off, int64_t size, bool last,
                               const std::string &token, std::string &err) {
    // For SFTP we maintain a persistent write handle stored as the "item_id"
    // Simple approach: re-open and seek on each chunk (acceptable for append-heavy writes)
    // Production: cache the handle in a per-path map
    const std::string &root = s.item_id.empty() ? "" : s.item_id; // set by filesystem layer
    auto &conn = GetConnection(root, token, err);
    if (!conn.IsValid()) return false;

    unsigned long open_flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT;
    if (off == 0) open_flags |= LIBSSH2_FXF_TRUNC;
    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(conn.sftp, s.upload_url.c_str(),
                                                  open_flags, 0644);
    if (!fh) { err = "sftp open for write failed: " + s.upload_url; return false; }

    libssh2_sftp_seek64(fh, (libssh2_uint64_t)off);
    int64_t written = 0;
    while (written < size) {
        int n = libssh2_sftp_write(fh, data + written, (size_t)(size - written));
        if (n < 0) { err = "sftp write error"; libssh2_sftp_close(fh); return false; }
        written += n;
    }
    libssh2_sftp_close(fh);
    return true;
}

// ─── Delete / CreateFolder ────────────────────────────────────────────────────
bool SFTPBackend::DeleteItem(const std::string &root, const std::string &id,
                              const std::string &token, std::string &err) {
    auto &conn = GetConnection(root, token, err);
    if (!conn.IsValid()) return false;
    // Try file first, then directory
    if (libssh2_sftp_unlink(conn.sftp, id.c_str()) == 0) return true;
    if (libssh2_sftp_rmdir(conn.sftp, id.c_str()) == 0)  return true;
    err = "sftp delete failed: " + id;
    return false;
}

bool SFTPBackend::CreateFolder(const std::string &root, const std::string &parent_id,
                                const std::string &name, const std::string &token,
                                CloudItem &out, std::string &err) {
    auto &conn = GetConnection(root, token, err);
    if (!conn.IsValid()) return false;
    std::string path = parent_id + "/" + name;
    if (libssh2_sftp_mkdir(conn.sftp, path.c_str(),
                            LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP) != 0) {
        err = "sftp mkdir failed: " + path; return false;
    }
    out.id = path; out.path = path; out.name = name; out.is_folder = true;
    return true;
}

} // namespace duckdb
