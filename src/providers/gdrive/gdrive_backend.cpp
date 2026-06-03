#include "providers/gdrive_backend.hpp"
#include "core/cloud_http.hpp"
#include <sstream>

namespace duckdb {

static const std::string kApiBase = "https://www.googleapis.com/drive/v3";

// URL: gdfs://MyDrive/path/to/file.parquet  or  gdfs://drive/DriveId/path
bool GDriveBackend::ParseUrl(const std::string &url,
                              std::string &out_root, std::string &out_path,
                              std::string &err) const {
    const std::string pfx = "gdfs://";
    if (url.substr(0, pfx.size()) != pfx) { err = "not gdfs://"; return false; }
    std::string rest = url.substr(pfx.size());
    auto slash = rest.find('/');
    out_root = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out_path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    return true;
}

// root = "MyDrive" → use "root" as the folder ID (user's root)
// root = "drive/DriveId" → use shared drive ID
bool GDriveBackend::ResolveRoot(const std::string &root, const std::string &tok,
                                 std::string &out_id, std::string &err) {
    if (root == "MyDrive" || root == "me" || root.empty()) {
        out_id = "root"; return true;  // "root" is the magic alias for user's root folder
    }
    // It's a shared drive ID directly
    out_id = root; return true;
}

bool GDriveBackend::ResolvePathWalk(const std::string &start_folder_id,
                                     const std::vector<std::string> &segments,
                                     const std::string &tok,
                                     CloudItem &out, std::string &err) {
    std::string current_id = start_folder_id;
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto &seg = segments[i];
        bool is_last    = (i + 1 == segments.size());

        // List children matching this name
        std::string q_url = kApiBase + "/files?q='" + current_id +
                            "'+in+parents+and+name='" + seg +
                            "'+and+trashed=false"
                            "&fields=files(id,name,size,mimeType,md5Checksum)"
                            "&supportsAllDrives=true&includeItemsFromAllDrives=true";
        auto resp = http_.Get(q_url, tok);
        if (!resp.ok()) { err = "walk failed at '" + seg + "': " + std::to_string(resp.status); return false; }

        auto files = JsonUtil::GetArray(resp.body, "files");
        if (files.empty()) { err = "not found: /" + seg; return false; }

        std::string id       = JsonUtil::GetString(files[0], "id");
        std::string mime     = JsonUtil::GetString(files[0], "mimeType");
        bool        is_folder = (mime == "application/vnd.google-apps.folder");

        if (is_last) {
            out.id        = id;
            out.name      = seg;
            out.size      = JsonUtil::GetInt64(files[0], "size");
            out.etag      = JsonUtil::GetString(files[0], "md5Checksum");
            out.mime_type = mime;
            out.is_folder = is_folder;
        }
        current_id = id;
    }
    return true;
}

bool GDriveBackend::Stat(const std::string &root, const std::string &path,
                          const std::string &tok, CloudItem &out, std::string &err) {
    if (path == "/" || path.empty()) {
        out.id = root; out.name = "/"; out.is_folder = true; return true;
    }
    // Split path into segments and walk
    std::vector<std::string> segs;
    std::istringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/'))
        if (!seg.empty()) segs.push_back(seg);

    if (!ResolvePathWalk(root, segs, tok, out, err)) return false;
    out.path = path;
    return true;
}

int64_t GDriveBackend::ReadRange(const CloudItem &item, const std::string &,
                                  const std::string &tok,
                                  int64_t off, int64_t len, char *buf, std::string &err) {
    return http_.ReadRange(DownloadUrl(item.id), tok, off, len, buf, err);
}

bool GDriveBackend::ListFolder(const std::string &root, const std::string &folder_id,
                                const std::string &tok,
                                const std::function<void(const CloudItem &)> &cb,
                                std::string &cursor, std::string &err) {
    std::string url = cursor.empty()
        ? kApiBase + "/files?q='" + folder_id + "'+in+parents+and+trashed=false"
          "&fields=nextPageToken,files(id,name,size,mimeType,md5Checksum)"
          "&supportsAllDrives=true&includeItemsFromAllDrives=true&pageSize=1000"
        : cursor;
    auto resp = http_.Get(url, tok);
    if (!resp.ok()) { err = "list failed " + std::to_string(resp.status); return false; }
    for (auto &j : JsonUtil::GetArray(resp.body, "files")) {
        CloudItem c;
        c.id        = JsonUtil::GetString(j, "id");
        c.name      = JsonUtil::GetString(j, "name");
        c.size      = JsonUtil::GetInt64(j, "size");
        c.etag      = JsonUtil::GetString(j, "md5Checksum");
        c.mime_type = JsonUtil::GetString(j, "mimeType");
        c.is_folder = (c.mime_type == "application/vnd.google-apps.folder");
        cb(c);
    }
    cursor = JsonUtil::GetString(resp.body, "nextPageToken");
    return true;
}

bool GDriveBackend::CreateUploadSession(const std::string &root,
                                         const std::string &parent_id,
                                         const std::string &name, int64_t total_size,
                                         const std::string &tok,
                                         CloudUploadSession &out, std::string &err) {
    // Google resumable upload: initiation request
    std::string url = "https://www.googleapis.com/upload/drive/v3/files"
                      "?uploadType=resumable&supportsAllDrives=true";
    std::string body = "{\"name\":\"" + name + "\",\"parents\":[\"" + parent_id + "\"]}";

    HttpRequest req;
    req.method = "POST"; req.url = url; req.body = body;
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";
    if (total_size > 0) {
        req.headers["X-Upload-Content-Length"] = std::to_string(total_size);
    }
    auto resp = http_.Execute(req);
    if (resp.status != 200) { err = "initiate upload failed " + std::to_string(resp.status); return false; }

    out.upload_url       = resp.header("location");
    out.chunk_size_bytes = 256 * 1024 * 4; // 1 MiB (multiple of 256 KiB)
    out.total_size_bytes = total_size;
    return !out.upload_url.empty();
}

bool GDriveBackend::UploadChunk(const CloudUploadSession &s, const char *data,
                                 int64_t off, int64_t sz, bool last,
                                 const std::string &, std::string &err) {
    std::unordered_map<std::string,std::string> hdrs;
    int64_t total = last && s.total_size_bytes > 0 ? s.total_size_bytes : -1;
    char cr[96]; snprintf(cr, sizeof(cr), "bytes %lld-%lld/%s",
        (long long)off, (long long)(off+sz-1),
        total >= 0 ? std::to_string(total).c_str() : "*");
    hdrs["Content-Range"] = cr;
    auto resp = http_.Put(s.upload_url, data, sz, hdrs);
    if (resp.status==200||resp.status==201||resp.status==308) return true;
    err = "uploadChunk " + std::to_string(resp.status); return false;
}

bool GDriveBackend::DeleteItem(const std::string &, const std::string &id,
                                const std::string &tok, std::string &err) {
    auto resp = http_.Delete(kApiBase + "/files/" + id + "?supportsAllDrives=true", tok);
    if (resp.status != 204) { err = "delete failed " + std::to_string(resp.status); return false; }
    return true;
}

bool GDriveBackend::CreateFolder(const std::string &, const std::string &parent_id,
                                  const std::string &name, const std::string &tok,
                                  CloudItem &out, std::string &err) {
    std::string url  = kApiBase + "/files?supportsAllDrives=true";
    std::string body = "{\"name\":\"" + name +
                       "\",\"mimeType\":\"application/vnd.google-apps.folder\",\"parents\":[\"" +
                       parent_id + "\"]}";
    auto resp = http_.Post(url, tok, body);
    if (!resp.ok()) { err = "createFolder failed"; return false; }
    out.id = JsonUtil::GetString(resp.body,"id"); out.name = name; out.is_folder = true;
    return true;
}

bool GDriveBackend::CopyItem(const std::string &, const std::string &src_id,
                               const std::string &dst_parent_id, const std::string &dst_name,
                               const std::string &tok, std::string &err) {
    std::string url  = kApiBase + "/files/" + src_id + "/copy?supportsAllDrives=true";
    std::string body = "{\"name\":\"" + dst_name + "\",\"parents\":[\"" + dst_parent_id + "\"]}";
    auto resp = http_.Post(url, tok, body);
    if (!resp.ok()) { err = "copy failed " + std::to_string(resp.status); return false; }
    return true;
}

} // namespace duckdb
