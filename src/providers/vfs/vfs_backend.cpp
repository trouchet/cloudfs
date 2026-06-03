#include "providers/vfs_backend.hpp"
#include "core/cloud_http.hpp"

namespace duckdb {

// ─── URL parsing ──────────────────────────────────────────────────────────────
bool VFSBackend::ParseUrl(const std::string &url,
                           std::string &out_root, std::string &out_path,
                           std::string &err) const {
    bool tls = (url.substr(0, 10) == "vfs+tls://");
    const std::string pfx = tls ? "vfs+tls://" : "vfs://";
    if (url.substr(0, pfx.size()) != pfx) { err = "not vfs://"; return false; }

    std::string rest  = url.substr(pfx.size()); // "host:port/path"
    auto slash        = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out_path              = (slash == std::string::npos) ? "/" : rest.substr(slash);
    out_root              = (tls ? "https://" : "http://") + authority;
    return true;
}

// ─── Item parsing ─────────────────────────────────────────────────────────────
CloudItem VFSBackend::ParseItem(const std::string &json) const {
    CloudItem c;
    c.id        = JsonUtil::GetString(json, "path");
    c.path      = c.id;
    c.name      = JsonUtil::GetString(json, "name");
    c.size      = JsonUtil::GetInt64(json, "size");
    c.etag      = JsonUtil::GetString(json, "etag");
    c.is_folder = JsonUtil::GetBool(json, "is_dir");
    c.modified_time_ms = JsonUtil::GetInt64(json, "mtime_ms");
    return c;
}

// ─── Stat ─────────────────────────────────────────────────────────────────────
bool VFSBackend::Stat(const std::string &root, const std::string &path,
                       const std::string &token, CloudItem &out, std::string &err) {
    auto resp = http_.Get(root + "/v1/stat?path=" + path, token);
    if (resp.status == 404) { err = "not found: " + path; return false; }
    if (!resp.ok()) { err = "stat failed (" + std::to_string(resp.status) + ")"; return false; }
    out = ParseItem(resp.body);
    return true;
}

// ─── ReadRange ────────────────────────────────────────────────────────────────
int64_t VFSBackend::ReadRange(const CloudItem &item, const std::string &root,
                               const std::string &token,
                               int64_t off, int64_t len, char *buf, std::string &err) {
    return http_.ReadRange(root + "/v1/read?path=" + item.path,
                           token, off, len, buf, err);
}

// ─── ListFolder ───────────────────────────────────────────────────────────────
bool VFSBackend::ListFolder(const std::string &root, const std::string &folder_id,
                             const std::string &token,
                             const std::function<void(const CloudItem &)> &cb,
                             std::string &cursor, std::string &err) {
    std::string url = root + "/v1/list?path=" + folder_id;
    if (!cursor.empty()) url += "&cursor=" + cursor;

    auto resp = http_.Get(url, token);
    if (!resp.ok()) { err = "list failed " + std::to_string(resp.status); return false; }

    for (auto &j : JsonUtil::GetArray(resp.body, "items")) cb(ParseItem(j));
    cursor = JsonUtil::GetString(resp.body, "next_cursor");
    return true;
}

// ─── Upload session ───────────────────────────────────────────────────────────
bool VFSBackend::CreateUploadSession(const std::string &root,
                                      const std::string &parent_id,
                                      const std::string &name, int64_t total_size,
                                      const std::string &token,
                                      CloudUploadSession &out, std::string &err) {
    std::string path = parent_id + "/" + name;
    std::string body = "{\"path\":\"" + path + "\",\"size\":" + std::to_string(total_size) + "}";
    auto resp = http_.Post(root + "/v1/upload/start", token, body);
    if (!resp.ok()) { err = "upload start failed"; return false; }
    out.upload_url        = root;          // agent base URL
    out.item_id           = JsonUtil::GetString(resp.body, "session_id");
    out.chunk_size_bytes  = 8 * 1024 * 1024; // 8 MiB
    out.total_size_bytes  = total_size;
    return !out.item_id.empty();
}

bool VFSBackend::UploadChunk(const CloudUploadSession &s, const char *data,
                              int64_t off, int64_t size, bool last,
                              const std::string &token, std::string &err) {
    std::string endpoint = last
        ? s.upload_url + "/v1/upload/finish?session=" + s.item_id
        : s.upload_url + "/v1/upload/chunk?session=" + s.item_id +
          "&offset=" + std::to_string(off);
    auto resp = http_.Put(endpoint, data, size, {{"Authorization", "Bearer " + token}});
    if (resp.status == 202 || resp.status == 201 || resp.status == 200) return true;
    err = "upload chunk failed " + std::to_string(resp.status); return false;
}

// ─── Delete / CreateFolder ────────────────────────────────────────────────────
bool VFSBackend::DeleteItem(const std::string &root, const std::string &id,
                             const std::string &token, std::string &err) {
    auto resp = http_.Delete(root + "/v1/delete?path=" + id, token);
    if (resp.status == 204 || resp.status == 200) return true;
    err = "delete failed " + std::to_string(resp.status); return false;
}

bool VFSBackend::CreateFolder(const std::string &root, const std::string &parent_id,
                               const std::string &name, const std::string &token,
                               CloudItem &out, std::string &err) {
    std::string path = parent_id + "/" + name;
    auto resp = http_.Post(root + "/v1/mkdir", token, "{\"path\":\"" + path + "\"}");
    if (!resp.ok()) { err = "mkdir failed"; return false; }
    out.id = path; out.path = path; out.name = name; out.is_folder = true;
    return true;
}

bool VFSBackend::Ping(const std::string &root, const std::string &token,
                       std::string &out_version, std::string &err) {
    auto resp = http_.Get(root + "/v1/ping", token);
    if (!resp.ok()) { err = "agent unreachable at " + root; return false; }
    out_version = JsonUtil::GetString(resp.body, "version");
    return true;
}

} // namespace duckdb
