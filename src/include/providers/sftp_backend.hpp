#pragma once
#include "../core/cloud_backend.hpp"
#include "../core/cloud_http.hpp"

#include <mutex>
#include <unordered_map>

#include <libssh2.h>
#include <libssh2_sftp.h>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// SFTPBackend
// Scheme: sftp://
// Auth:   SSH private key file, SSH agent, or password
// Library: libssh2
//
// URL format:
//   sftp://user@hostname/absolute/path/to/file.parquet
//   sftp://user@hostname:2222/path/to/file.parquet   (custom port)
//   sftp://ec2-user@54.123.0.1/home/ec2-user/data/*.parquet
//
// Works on any Linux/BSD VPS with sshd running — no extra software.
//
// Range reads: SFTP has no native Range concept.  We implement it via
//   libssh2_sftp_seek64() + libssh2_sftp_read() which reads from an
//   arbitrary offset without downloading the whole file.  This gives
//   O(1) random access identical to HTTP 206 Partial Content.
//
// Uploads: SFTP write is already streaming — no upload session needed.
//   We open a remote file handle, write chunks, close.  chunk_alignment=1.
//
// Concurrency: each CloudFileHandle gets its own SFTP channel on the
//   shared SSH session.  The session itself is protected by a mutex.
// ─────────────────────────────────────────────────────────────────────────────

struct SSHConnection {
    int socket_fd = -1;
    LIBSSH2_SESSION* session = nullptr;
    LIBSSH2_SFTP* sftp = nullptr;

    bool IsValid() const { return session && sftp; }
    void Close();
};

class SFTPBackend : public ICloudBackend {
  public:
    SFTPBackend() = default;
    ~SFTPBackend() override;

    std::string Scheme() const override { return "sftp"; }
    std::string Name() const override { return "SFTP"; }

    ProviderCapabilities Capabilities() const override {
        ProviderCapabilities caps;
        caps.supports_range_reads = true;        // via sftp_seek64
        caps.supports_resumable_uploads = false; // simple streaming write
        caps.supports_server_side_copy = false;
        caps.supports_recursive_list = false;
        caps.needs_total_size_upfront = false;
        caps.upload_chunk_alignment = 1;
        caps.min_upload_chunk = 1;
        caps.max_upload_chunk = 32 * 1024 * 1024;
        return caps;
    }

    // sftp://user@host:port/path  →  root="user@host:port"  path="/path"
    bool ParseUrl(const std::string& url, std::string& out_root, std::string& out_path,
                  std::string& err) const override;

    // root string → canonical "user@host:port" (no-op, already canonical)
    bool ResolveRoot(const std::string& root, const std::string&, std::string& out_id,
                     std::string& err) override {
        out_id = root;
        return true;
    }

    bool Stat(const std::string& root, const std::string& path, const std::string& token,
              CloudItem& out, std::string& err) override;

    int64_t ReadRange(const CloudItem& item, const std::string& root, const std::string& token,
                      int64_t off, int64_t len, char* buf, std::string& err) override;

    bool ListFolder(const std::string& root, const std::string& folder_id, const std::string& token,
                    const std::function<void(const CloudItem&)>& cb, std::string& cursor,
                    std::string& err) override;

    bool CreateUploadSession(const std::string& root, const std::string& parent_id,
                             const std::string& name, int64_t total_size, const std::string& token,
                             CloudUploadSession& out, std::string& err) override;

    bool UploadChunk(const CloudUploadSession& s, const char* data, int64_t off, int64_t size,
                     bool last, const std::string& token, std::string& err) override;

    bool DeleteItem(const std::string& root, const std::string& id, const std::string& token,
                    std::string& err) override;

    bool CreateFolder(const std::string& root, const std::string& parent_id,
                      const std::string& name, const std::string& token, CloudItem& out,
                      std::string& err) override;

  private:
    // Get or create an SSH+SFTP connection for a given root ("user@host:port")
    SSHConnection& GetConnection(const std::string& root,
                                 const std::string& token, // token = private_key_path:passphrase
                                 std::string& err);
    void CloseConnection(const std::string& root);

    std::unordered_map<std::string, SSHConnection> connections_;
    std::mutex conn_mutex_;
};

// ─────────────────────────────────────────────────────────────────────────────
// SFTPAuth
// token format (passed to GetAccessToken):
//   "keyfile:/home/user/.ssh/id_rsa"              (private key, no passphrase)
//   "keyfile:/home/user/.ssh/id_rsa:mypassphrase" (private key + passphrase)
//   "agent:"                                       (use SSH agent)
//   "password:mysecretpassword"                    (password auth — less secure)
// ─────────────────────────────────────────────────────────────────────────────
class SFTPAuth : public ICloudAuthProvider {
  public:
    explicit SFTPAuth(std::string auth_string) : auth_(std::move(auth_string)) {}

    bool GetAccessToken(std::string& out, std::string& err) override {
        out = auth_;
        return !auth_.empty();
    }
    std::string ProviderName() const override { return "sftp"; }

  private:
    std::string auth_;
};

} // namespace duckdb
