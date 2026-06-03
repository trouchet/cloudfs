#include "providers/onedrive_backend.hpp"
#include "core/cloud_http.hpp"

namespace duckdb {

// URL: odfs://me/path  or  odfs://user@tenant.com/path
bool OneDriveBackend::ParseUrl(const std::string &url,
                                std::string &out_root, std::string &out_path,
                                std::string &err) const {
    const std::string pfx = "odfs://";
    if (url.substr(0, pfx.size()) != pfx) { err = "not odfs://"; return false; }
    std::string rest = url.substr(pfx.size());
    auto slash = rest.find('/');
    out_root = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out_path = (slash == std::string::npos) ? "/"  : rest.substr(slash);
    return true;
}

std::string OneDriveBackend::DriveBaseUrl(const std::string &root_id) const {
    if (root_id == "me" || root_id.empty())
        return "https://graph.microsoft.com/v1.0/me/drive";
    return "https://graph.microsoft.com/v1.0/users/" + root_id + "/drive";
}

bool OneDriveBackend::ResolveRoot(const std::string &root, const std::string &tok,
                                   std::string &out_id, std::string &err) {
    // Get the drive ID for the user's OneDrive
    auto resp = http_.Get(DriveBaseUrl(root), tok);
    if (!resp.ok()) { err = "get drive failed (" + std::to_string(resp.status) + ")"; return false; }
    out_id = JsonUtil::GetString(resp.body, "id");
    return !out_id.empty();
}

bool OneDriveBackend::Stat(const std::string &root, const std::string &path,
                            const std::string &tok, CloudItem &out, std::string &err) {
    std::string url = "https://graph.microsoft.com/v1.0/drives/" + root +
                      "/root:" + path +
                      "?$select=id,name,size,eTag,folder,file,@microsoft.graph.downloadUrl";
    auto resp = http_.Get(url, tok);
    if (resp.status == 404) { err = "not found: " + path; return false; }
    if (!resp.ok()) { err = "stat failed " + std::to_string(resp.status); return false; }
    out.id        = JsonUtil::GetString(resp.body, "id");
    out.name      = JsonUtil::GetString(resp.body, "name");
    out.size      = JsonUtil::GetInt64(resp.body, "size");
    out.etag      = JsonUtil::GetString(resp.body, "eTag");
    out.download_url = JsonUtil::GetString(resp.body, "@microsoft.graph.downloadUrl");
    out.path      = path;
    out.is_folder = (resp.body.find("\"folder\":{") != std::string::npos);
    return true;
}

int64_t OneDriveBackend::ReadRange(const CloudItem &item, const std::string &root,
                                    const std::string &tok,
                                    int64_t off, int64_t len, char *buf, std::string &err) {
    if (!item.download_url.empty())
        return http_.ReadRange(item.download_url, "", off, len, buf, err);
    std::string url = "https://graph.microsoft.com/v1.0/drives/" + root +
                      "/items/" + item.id + "/content";
    return http_.ReadRange(url, tok, off, len, buf, err);
}

bool OneDriveBackend::RefreshDownloadUrl(const CloudItem &item, const std::string &root,
                                          const std::string &tok, std::string &out_url, std::string &err) {
    CloudItem updated;
    if (!Stat(root, item.path, tok, updated, err)) return false;
    out_url = updated.download_url; return !out_url.empty();
}

bool OneDriveBackend::ListFolder(const std::string &root, const std::string &folder_id,
                                  const std::string &tok,
                                  const std::function<void(const CloudItem &)> &cb,
                                  std::string &cursor, std::string &err) {
    std::string url = cursor.empty()
        ? "https://graph.microsoft.com/v1.0/drives/" + root + "/items/" +
          folder_id + "/children?$select=id,name,size,eTag,folder,file&$top=1000"
        : cursor;
    auto resp = http_.Get(url, tok);
    if (!resp.ok()) { err = "list failed"; return false; }
    for (auto &j : JsonUtil::GetArray(resp.body, "value")) {
        CloudItem c;
        c.id = JsonUtil::GetString(j,"id"); c.name = JsonUtil::GetString(j,"name");
        c.size = JsonUtil::GetInt64(j,"size"); c.etag = JsonUtil::GetString(j,"eTag");
        c.is_folder = (j.find("\"folder\":{") != std::string::npos);
        cb(c);
    }
    cursor = JsonUtil::GetString(resp.body, "@odata.nextLink");
    return true;
}

bool OneDriveBackend::CreateUploadSession(const std::string &root,
                                           const std::string &parent_id,
                                           const std::string &name, int64_t,
                                           const std::string &tok,
                                           CloudUploadSession &out, std::string &err) {
    std::string url = "https://graph.microsoft.com/v1.0/drives/" + root +
                      "/items/" + parent_id + ":/" + name + ":/createUploadSession";
    auto resp = http_.Post(url, tok, "{\"item\":{\"@microsoft.graph.conflictBehavior\":\"replace\"}}");
    if (!resp.ok()) { err = "createUploadSession failed"; return false; }
    out.upload_url = JsonUtil::GetString(resp.body, "uploadUrl");
    out.chunk_size_bytes = 4 * 320 * 1024;
    return !out.upload_url.empty();
}

bool OneDriveBackend::UploadChunk(const CloudUploadSession &s, const char *data,
                                   int64_t off, int64_t sz, bool,
                                   const std::string &, std::string &err) {
    std::unordered_map<std::string,std::string> hdrs;
    char cr[96]; snprintf(cr, sizeof(cr), "bytes %lld-%lld/%lld",
        (long long)off,(long long)(off+sz-1),(long long)s.total_size_bytes);
    hdrs["Content-Range"] = cr;
    auto resp = http_.Put(s.upload_url, data, sz, hdrs);
    if (resp.status==202||resp.status==201||resp.status==200) return true;
    err = "uploadChunk " + std::to_string(resp.status); return false;
}

bool OneDriveBackend::DeleteItem(const std::string &root, const std::string &id,
                                  const std::string &tok, std::string &err) {
    auto resp = http_.Delete("https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + id, tok);
    if (resp.status != 204) { err = "delete failed " + std::to_string(resp.status); return false; }
    return true;
}

bool OneDriveBackend::CreateFolder(const std::string &root, const std::string &parent_id,
                                    const std::string &name, const std::string &tok,
                                    CloudItem &out, std::string &err) {
    std::string url = "https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + parent_id + "/children";
    auto resp = http_.Post(url, tok, "{\"name\":\"" + name + "\",\"folder\":{}}");
    if (!resp.ok()) { err = "createFolder failed"; return false; }
    out.id = JsonUtil::GetString(resp.body, "id"); out.name = name; out.is_folder = true;
    return true;
}

} // namespace duckdb
