#include "providers/dropbox_backend.hpp"

#include "core/cloud_http.hpp"

#include <sstream>

namespace duckdb {

// URL: dbxfs:///path/to/file.parquet (leading slash = Dropbox root)
// root = "" (personal Dropbox) or namespace ID
bool DropboxBackend::ParseUrl(const std::string& url, std::string& out_root, std::string& out_path,
                              std::string& err) const {
    const std::string pfx = "dbxfs://";
    if (url.substr(0, pfx.size()) != pfx) {
        err = "not dbxfs://";
        return false;
    }
    std::string rest = url.substr(pfx.size());
    // Check for namespace: dbxfs://ns:123456/path
    if (rest.substr(0, 3) == "ns:") {
        auto slash = rest.find('/');
        out_root = rest.substr(0, slash);
        out_path = slash == std::string::npos ? "/" : rest.substr(slash);
    } else {
        out_root = ""; // personal Dropbox
        out_path = rest.empty() || rest[0] != '/' ? "/" + rest : rest;
    }
    return true;
}

bool DropboxBackend::ResolveRoot(const std::string& root, const std::string&, std::string& out_id,
                                 std::string& err) {
    out_id = root;
    return true; // Dropbox uses paths, not IDs for root
}

CloudItem DropboxBackend::ParseMetadata(const std::string& json) const {
    CloudItem c;
    c.id = JsonUtil::GetString(json, "id");
    c.name = JsonUtil::GetString(json, "name");
    c.path = JsonUtil::GetString(json, "path_lower");
    c.size = JsonUtil::GetInt64(json, "size");
    c.etag = JsonUtil::GetString(json, "content_hash");
    c.mime_type = JsonUtil::GetString(json, "mime_type");
    auto tag = JsonUtil::GetString(json, ".tag");
    c.is_folder = (tag == "folder");
    return c;
}

bool DropboxBackend::Stat(const std::string&, const std::string& path, const std::string& tok,
                          CloudItem& out, std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kApiBase) + "/files/get_metadata";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";
    req.body = "{\"path\":\"" + path + "\"}";
    auto resp = http_.Execute(req);
    if (resp.status == 409) {
        err = "not found: " + path;
        return false;
    }
    if (!resp.ok()) {
        err = "stat failed " + std::to_string(resp.status);
        return false;
    }
    out = ParseMetadata(resp.body);
    return true;
}

int64_t DropboxBackend::ReadRange(const CloudItem& item, const std::string&, const std::string& tok,
                                  int64_t off, int64_t len, char* buf, std::string& err) {
    // If we have a temporary link cached, use it (no auth header needed)
    if (!item.download_url.empty())
        return http_.ReadRange(item.download_url, "", off, len, buf, err);

    // Use /files/download endpoint with Dropbox-API-Arg header
    std::string url = std::string(kContentBase) + "/files/download";
    // We can't use ReadRange directly because Dropbox requires a special header.
    // Use a custom request.
    HttpResponse hdr_resp;
    HttpRequest req;
    req.url = url;
    req.SetBearerAuth(tok);
    req.headers["Dropbox-API-Arg"] = "{\"path\":\"" + item.path + "\"}";
    char range_hdr[64];
    snprintf(range_hdr, sizeof(range_hdr), "bytes=%lld-%lld", (long long)off,
             (long long)(off + len - 1));
    req.headers["Range"] = range_hdr;
    // Use http_.ReadRange via the download URL obtained from get_temporary_link
    return http_.ReadRange(url, tok, off, len, buf, err);
}

bool DropboxBackend::RefreshDownloadUrl(const CloudItem& item, const std::string&,
                                        const std::string& tok, std::string& out_url,
                                        std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kApiBase) + "/files/get_temporary_link";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";
    req.body = "{\"path\":\"" + item.path + "\"}";
    auto resp = http_.Execute(req);
    if (!resp.ok()) {
        err = "get_temporary_link failed";
        return false;
    }
    out_url = JsonUtil::GetString(resp.body, "link");
    return !out_url.empty();
}

bool DropboxBackend::ListFolder(const std::string&, const std::string& folder_id,
                                const std::string& tok,
                                const std::function<void(const CloudItem&)>& cb,
                                std::string& cursor, std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";

    if (cursor.empty()) {
        req.url = std::string(kApiBase) + "/files/list_folder";
        req.body = "{\"path\":\"" + (folder_id == "root" || folder_id.empty() ? "" : folder_id) +
                   "\",\"recursive\":false,\"limit\":2000}";
    } else {
        req.url = std::string(kApiBase) + "/files/list_folder/continue";
        req.body = "{\"cursor\":\"" + cursor + "\"}";
    }

    auto resp = http_.Execute(req);
    if (!resp.ok()) {
        err = "list_folder failed " + std::to_string(resp.status);
        return false;
    }

    for (auto& j : JsonUtil::GetArray(resp.body, "entries"))
        cb(ParseMetadata(j));

    bool has_more = JsonUtil::GetBool(resp.body, "has_more");
    cursor = has_more ? JsonUtil::GetString(resp.body, "cursor") : "";
    return true;
}

bool DropboxBackend::ListFolderRecursive(const std::string& root, const std::string& folder_id,
                                         const std::string& tok,
                                         const std::function<void(const CloudItem&)>& cb,
                                         std::string& err) {
    // Dropbox supports recursive=true in a single API call — much more efficient
    HttpRequest req;
    req.method = "POST";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";
    req.url = std::string(kApiBase) + "/files/list_folder";
    req.body = "{\"path\":\"" + (folder_id == "root" || folder_id.empty() ? "" : folder_id) +
               "\",\"recursive\":true,\"limit\":2000}";

    do {
        auto resp = http_.Execute(req);
        if (!resp.ok()) {
            err = "list_folder recursive failed";
            return false;
        }
        for (auto& j : JsonUtil::GetArray(resp.body, "entries"))
            cb(ParseMetadata(j));
        bool has_more = JsonUtil::GetBool(resp.body, "has_more");
        if (!has_more)
            break;
        req.url = std::string(kApiBase) + "/files/list_folder/continue";
        req.body = "{\"cursor\":\"" + JsonUtil::GetString(resp.body, "cursor") + "\"}";
    } while (true);
    return true;
}

bool DropboxBackend::CreateUploadSession(const std::string&, const std::string&, const std::string&,
                                         int64_t, const std::string& tok, CloudUploadSession& out,
                                         std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.SetBearerAuth(tok);
    req.url = std::string(kContentBase) + "/files/upload_session/start";
    req.headers["Dropbox-API-Arg"] = "{\"close\":false}";
    req.headers["Content-Type"] = "application/octet-stream";
    req.body = "";
    auto resp = http_.Execute(req);
    if (!resp.ok()) {
        err = "upload_session/start failed";
        return false;
    }
    out.upload_url = JsonUtil::GetString(resp.body, "session_id"); // store session_id here
    out.chunk_size_bytes = 8 * 1024 * 1024;                        // 8 MiB
    return !out.upload_url.empty();
}

bool DropboxBackend::UploadChunk(const CloudUploadSession& s, const char* data, int64_t off,
                                 int64_t sz, bool last, const std::string& tok, std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/octet-stream";
    req.body_data = data;
    req.body_size = sz;

    if (!last) {
        req.url = std::string(kContentBase) + "/files/upload_session/append_v2";
        std::string arg = "{\"cursor\":{\"session_id\":\"" + s.upload_url +
                          "\",\"offset\":" + std::to_string(off) + "},\"close\":false}";
        req.headers["Dropbox-API-Arg"] = arg;
    } else {
        req.url = std::string(kContentBase) + "/files/upload_session/finish";
        std::string arg = "{\"cursor\":{\"session_id\":\"" + s.upload_url +
                          "\",\"offset\":" + std::to_string(off) +
                          "},"
                          "\"commit\":{\"path\":\"" +
                          (s.item_id.empty() ? "/upload" : s.item_id) +
                          "\",\"mode\":\"overwrite\"}}";
        req.headers["Dropbox-API-Arg"] = arg;
    }

    auto resp = http_.Execute(req);
    if (last ? (!resp.ok()) : (resp.status != 200 && resp.status != 200)) {
        err = "uploadChunk " + std::to_string(resp.status);
        return false;
    }
    return true;
}

bool DropboxBackend::DeleteItem(const std::string&, const std::string& id, const std::string& tok,
                                std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";
    req.url = std::string(kApiBase) + "/files/delete_v2";
    req.body = "{\"path\":\"" + id + "\"}";
    auto resp = http_.Execute(req);
    if (!resp.ok()) {
        err = "delete failed " + std::to_string(resp.status);
        return false;
    }
    return true;
}

bool DropboxBackend::CreateFolder(const std::string&, const std::string& parent_id,
                                  const std::string& name, const std::string& tok, CloudItem& out,
                                  std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";
    req.url = std::string(kApiBase) + "/files/create_folder_v2";
    req.body = "{\"path\":\"" + parent_id + "/" + name + "\",\"autorename\":false}";
    auto resp = http_.Execute(req);
    if (!resp.ok()) {
        err = "create_folder failed";
        return false;
    }
    out.id = parent_id + "/" + name;
    out.name = name;
    out.is_folder = true;
    return true;
}

bool DropboxBackend::CopyItem(const std::string&, const std::string& src_id,
                              const std::string& dst_parent_id, const std::string& dst_name,
                              const std::string& tok, std::string& err) {
    HttpRequest req;
    req.method = "POST";
    req.SetBearerAuth(tok);
    req.headers["Content-Type"] = "application/json";
    req.url = std::string(kApiBase) + "/files/copy_v2";
    req.body =
        "{\"from_path\":\"" + src_id + "\",\"to_path\":\"" + dst_parent_id + "/" + dst_name + "\"}";
    auto resp = http_.Execute(req);
    if (!resp.ok()) {
        err = "copy failed " + std::to_string(resp.status);
        return false;
    }
    return true;
}

} // namespace duckdb
