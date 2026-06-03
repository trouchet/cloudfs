#include "providers/sharepoint_backend.hpp"

#include "core/cloud_http.hpp"

namespace duckdb {

// ─── URL parsing ──────────────────────────────────────────────────────────────
// spfs://tenant.sharepoint.com/sites/SiteName/Library/path/file.parquet
// out_root = "https://tenant.sharepoint.com|/sites/SiteName"
// out_path = "/Library/path/file.parquet"
bool SharePointBackend::ParseUrl(const std::string& url, std::string& out_root,
                                 std::string& out_path, std::string& err) const {
    const std::string pfx = "spfs://";
    if (url.substr(0, pfx.size()) != pfx) {
        err = "not a spfs:// URL";
        return false;
    }
    std::string rest = url.substr(pfx.size());
    auto slash1 = rest.find('/');
    if (slash1 == std::string::npos) {
        out_root = "https://" + rest;
        out_path = "/";
        return true;
    }
    std::string host = rest.substr(0, slash1);
    std::string path = rest.substr(slash1); // "/sites/X/..."

    static const char* prefixes[] = {"/sites/", "/teams/", "/personal/", nullptr};
    for (int i = 0; prefixes[i]; ++i) {
        std::string sp = prefixes[i];
        if (path.size() > sp.size() && path.substr(0, sp.size()) == sp) {
            auto next = path.find('/', sp.size());
            if (next == std::string::npos) {
                out_root = "https://" + host + "|" + path;
                out_path = "/";
            } else {
                out_root = "https://" + host + "|" + path.substr(0, next);
                out_path = path.substr(next);
            }
            return true;
        }
    }
    out_root = "https://" + host + "|/";
    out_path = path;
    return true;
}

// ─── Root resolution: root string → drive_id ─────────────────────────────────
bool SharePointBackend::ResolveRoot(const std::string& root, const std::string& tok,
                                    std::string& out_id, std::string& err) {
    auto pipe = root.find('|');
    if (pipe == std::string::npos) {
        err = "invalid spfs root: " + root;
        return false;
    }
    std::string base_url = root.substr(0, pipe);   // "https://tenant.sharepoint.com"
    std::string site_path = root.substr(pipe + 1); // "/sites/SiteName"

    std::string hostname = base_url.substr(8); // "tenant.sharepoint.com"
    std::string site_url = "https://graph.microsoft.com/v1.0/sites/" + hostname + ":" + site_path;

    auto resp = http_.Get(site_url, tok);
    if (!resp.ok()) {
        err = "get site failed (" + std::to_string(resp.status) + "): " + resp.body.substr(0, 200);
        return false;
    }
    std::string site_id = JsonUtil::GetString(resp.body, "id");

    auto drive_resp =
        http_.Get("https://graph.microsoft.com/v1.0/sites/" + site_id + "/drive", tok);
    if (!drive_resp.ok()) {
        err = "get drive failed";
        return false;
    }
    out_id = JsonUtil::GetString(drive_resp.body, "id");
    return !out_id.empty();
}

// ─── Stat ─────────────────────────────────────────────────────────────────────
bool SharePointBackend::Stat(const std::string& root, const std::string& path,
                             const std::string& tok, CloudItem& out, std::string& err) {
    std::string url = "https://graph.microsoft.com/v1.0/drives/" + root + "/root:" + path +
                      "?$select=id,name,size,eTag,folder,file,@microsoft.graph.downloadUrl";
    auto resp = http_.Get(url, tok);
    if (resp.status == 404) {
        err = "not found: " + path;
        return false;
    }
    if (!resp.ok()) {
        err = "stat failed (" + std::to_string(resp.status) + ")";
        return false;
    }
    out.id = JsonUtil::GetString(resp.body, "id");
    out.name = JsonUtil::GetString(resp.body, "name");
    out.size = JsonUtil::GetInt64(resp.body, "size");
    out.etag = JsonUtil::GetString(resp.body, "eTag");
    out.download_url = JsonUtil::GetString(resp.body, "@microsoft.graph.downloadUrl");
    out.path = path;
    out.is_folder = (resp.body.find("\"folder\":{") != std::string::npos);
    return true;
}

// ─── ReadRange ────────────────────────────────────────────────────────────────
int64_t SharePointBackend::ReadRange(const CloudItem& item, const std::string& root,
                                     const std::string& tok, int64_t off, int64_t len, char* buf,
                                     std::string& err) {
    if (!item.download_url.empty())
        return http_.ReadRange(item.download_url, "", off, len, buf, err);
    // Fallback: stream via Graph API content endpoint
    std::string url =
        "https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + item.id + "/content";
    return http_.ReadRange(url, tok, off, len, buf, err);
}

bool SharePointBackend::RefreshDownloadUrl(const CloudItem& item, const std::string& root,
                                           const std::string& tok, std::string& out_url,
                                           std::string& err) {
    CloudItem updated;
    if (!Stat(root, item.path, tok, updated, err))
        return false;
    out_url = updated.download_url;
    return !out_url.empty();
}

// ─── ListFolder ───────────────────────────────────────────────────────────────
bool SharePointBackend::ListFolder(const std::string& root, const std::string& folder_id,
                                   const std::string& tok,
                                   const std::function<void(const CloudItem&)>& cb,
                                   std::string& cursor, std::string& err) {
    std::string url =
        cursor.empty() ? "https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + folder_id +
                             "/children?$select=id,name,size,eTag,folder,file&$top=1000"
                       : cursor;

    auto resp = http_.Get(url, tok);
    if (!resp.ok()) {
        err = "list failed (" + std::to_string(resp.status) + ")";
        return false;
    }

    for (auto& j : JsonUtil::GetArray(resp.body, "value")) {
        CloudItem c;
        c.id = JsonUtil::GetString(j, "id");
        c.name = JsonUtil::GetString(j, "name");
        c.size = JsonUtil::GetInt64(j, "size");
        c.etag = JsonUtil::GetString(j, "eTag");
        c.is_folder = (j.find("\"folder\":{") != std::string::npos);
        cb(c);
    }
    cursor = JsonUtil::GetString(resp.body, "@odata.nextLink");
    return true;
}

// ─── Upload session ───────────────────────────────────────────────────────────
bool SharePointBackend::CreateUploadSession(const std::string& root, const std::string& parent_id,
                                            const std::string& name, int64_t,
                                            const std::string& tok, CloudUploadSession& out,
                                            std::string& err) {
    std::string url = "https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + parent_id +
                      ":/" + name + ":/createUploadSession";
    std::string body = "{\"item\":{\"@microsoft.graph.conflictBehavior\":\"replace\"}}";
    auto resp = http_.Post(url, tok, body);
    if (!resp.ok()) {
        err = "createUploadSession failed";
        return false;
    }
    out.upload_url = JsonUtil::GetString(resp.body, "uploadUrl");
    out.chunk_size_bytes = 4 * 320 * 1024; // 4× 320 KiB
    return !out.upload_url.empty();
}

bool SharePointBackend::UploadChunk(const CloudUploadSession& s, const char* data, int64_t off,
                                    int64_t sz, bool /*last*/, const std::string&,
                                    std::string& err) {
    std::unordered_map<std::string, std::string> hdrs;
    char cr[96];
    snprintf(cr, sizeof(cr), "bytes %lld-%lld/%lld", (long long)off, (long long)(off + sz - 1),
             (long long)s.total_size_bytes);
    hdrs["Content-Range"] = cr;
    auto resp = http_.Put(s.upload_url, data, sz, hdrs);
    if (resp.status == 202 || resp.status == 201 || resp.status == 200)
        return true;
    err = "uploadChunk failed (" + std::to_string(resp.status) + "): " + resp.body.substr(0, 200);
    return false;
}

// ─── Delete / CreateFolder / Copy ─────────────────────────────────────────────
bool SharePointBackend::DeleteItem(const std::string& root, const std::string& id,
                                   const std::string& tok, std::string& err) {
    auto resp =
        http_.Delete("https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + id, tok);
    if (resp.status != 204) {
        err = "delete failed (" + std::to_string(resp.status) + ")";
        return false;
    }
    return true;
}

bool SharePointBackend::CreateFolder(const std::string& root, const std::string& parent_id,
                                     const std::string& name, const std::string& tok,
                                     CloudItem& out, std::string& err) {
    std::string url =
        "https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + parent_id + "/children";
    std::string body = "{\"name\":\"" + name +
                       "\",\"folder\":{},\"@microsoft.graph.conflictBehavior\":\"replace\"}";
    auto resp = http_.Post(url, tok, body);
    if (!resp.ok()) {
        err = "createFolder failed";
        return false;
    }
    out.id = JsonUtil::GetString(resp.body, "id");
    out.name = name;
    out.is_folder = true;
    return true;
}

bool SharePointBackend::CopyItem(const std::string& root, const std::string& src_id,
                                 const std::string& dst_parent_id, const std::string& dst_name,
                                 const std::string& tok, std::string& err) {
    std::string url =
        "https://graph.microsoft.com/v1.0/drives/" + root + "/items/" + src_id + "/copy";
    std::string body =
        "{\"parentReference\":{\"id\":\"" + dst_parent_id + "\"},\"name\":\"" + dst_name + "\"}";
    auto resp = http_.Post(url, tok, body);
    if (resp.status != 202) {
        err = "copy failed (" + std::to_string(resp.status) + ")";
        return false;
    }
    return true;
}

} // namespace duckdb
