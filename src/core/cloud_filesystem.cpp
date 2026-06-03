#include "core/cloud_filesystem.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/timestamp.hpp"

#include <algorithm>
#include <cstring>

#include <fnmatch.h>

namespace duckdb {

static void Throw(const std::string& msg) {
    if (!msg.empty())
        throw IOException(msg);
}

// ─── CloudFileHandle ──────────────────────────────────────────────────────────
CloudFileHandle::CloudFileHandle(CloudFileSystem& fs, const std::string& path,
                                 ICloudBackend& backend_, const CloudItem& item_,
                                 const std::string& root_id_, FileOpenFlags flags)
    : FileHandle(fs, path, flags), file_size(item_.size), root_id(root_id_), item(item_),
      open_flags(flags), backend(backend_) {}

CloudFileHandle::~CloudFileHandle() {
    Close();
}

void CloudFileHandle::Close() {
    if (upload_active && !write_buffer.empty()) {
        auto& cfs = dynamic_cast<CloudFileSystem&>(file_system);
        cfs.FileSync(*this);
    }
}

// ─── CloudFileSystem ──────────────────────────────────────────────────────────
CloudFileSystem::CloudFileSystem() = default;
CloudFileSystem::~CloudFileSystem() = default;

void CloudFileSystem::RegisterBackend(unique_ptr<ICloudBackend> b) {
    std::lock_guard<std::mutex> lk(mu_);
    backends_[b->Scheme()] = std::move(b);
}

void CloudFileSystem::SetAuth(const std::string& scheme, std::shared_ptr<ICloudAuthProvider> auth) {
    std::lock_guard<std::mutex> lk(mu_);
    auth_map_[scheme] = std::move(auth);
}

std::shared_ptr<ICloudAuthProvider> CloudFileSystem::GetAuth(const std::string& scheme) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = auth_map_.find(scheme);
    return it == auth_map_.end() ? nullptr : it->second;
}

std::vector<std::string> CloudFileSystem::RegisteredSchemes() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> v;
    for (auto& [k, _] : backends_)
        v.push_back(k);
    std::sort(v.begin(), v.end());
    return v;
}

bool CloudFileSystem::CanHandleFile(const string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [_, b] : backends_)
        if (b->CanHandle(path))
            return true;
    return false;
}

ICloudBackend& CloudFileSystem::BackendFor(const std::string& path) {
    auto* b = BackendForNoThrow(path);
    if (!b)
        Throw("cloudfs: no backend registered for URL: " + path);
    return *b;
}

ICloudBackend* CloudFileSystem::BackendForNoThrow(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [_, b] : backends_)
        if (b->CanHandle(path))
            return b.get();
    return nullptr;
}

bool CloudFileSystem::GetToken(const std::string& scheme, std::string& out, std::string& err) {
    std::shared_ptr<ICloudAuthProvider> auth;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = auth_map_.find(scheme);
        if (it == auth_map_.end()) {
            err = "cloudfs: no auth for '" + scheme + "'. Run: CREATE SECRET (TYPE " + scheme +
                  ", ...)";
            return false;
        }
        auth = it->second;
    }
    return auth->GetAccessToken(out, err);
}

// ─── ResolvePath ──────────────────────────────────────────────────────────────
bool CloudFileSystem::ResolvePath(ICloudBackend& backend, const std::string& url,
                                  CloudItem& out_item, std::string& out_root_id, std::string& err) {
    // 1. Check path cache
    if (cache_.GetItem(backend.Scheme(), url, out_item)) {
        out_root_id = out_item.parent_id; // we stash root_id in parent_id for cache
        return true;
    }

    // 2. Parse URL
    std::string raw_root, path;
    if (!backend.ParseUrl(url, raw_root, path, err))
        return false;

    // 3. Resolve root (cached separately)
    std::string root_id;
    if (!cache_.GetRootId(backend.Scheme(), raw_root, root_id)) {
        std::string tok;
        if (!GetToken(backend.Scheme(), tok, err))
            return false;
        if (!backend.ResolveRoot(raw_root, tok, root_id, err))
            return false;
        cache_.PutRootId(backend.Scheme(), raw_root, root_id);
    }
    out_root_id = root_id;

    // 4. Stat the item
    std::string tok;
    if (!GetToken(backend.Scheme(), tok, err))
        return false;
    if (!backend.Stat(root_id, path, tok, out_item, err))
        return false;

    // Stash root_id for cache retrieval
    out_item.parent_id = root_id;
    cache_.PutItem(backend.Scheme(), url, out_item);
    return true;
}

bool CloudFileSystem::EnsureDownloadUrl(ICloudBackend& backend, const std::string& root_id,
                                        CloudItem& item, const std::string& token,
                                        std::string& err) {
    // Check cache first
    if (cache_.GetDownloadUrl(backend.Scheme(), item.id, item.download_url))
        return true;
    // Use pre-fetched URL from Stat() if fresh
    if (!item.download_url.empty()) {
        cache_.PutDownloadUrl(backend.Scheme(), item.id, item.download_url);
        return true;
    }
    // Ask backend to refresh
    if (!backend.RefreshDownloadUrl(item, root_id, token, item.download_url, err)) {
        item.download_url.clear(); // backend doesn't support pre-auth URLs — use ReadRange()
        return true;               // still OK: ReadRange() will use token directly
    }
    cache_.PutDownloadUrl(backend.Scheme(), item.id, item.download_url);
    return true;
}

// ─── OpenFile ─────────────────────────────────────────────────────────────────
unique_ptr<FileHandle> CloudFileSystem::OpenFile(const string& path, FileOpenFlags flags,
                                                 optional_ptr<FileOpener>) {
    auto& backend = BackendFor(path);
    std::string err;

    if (flags.OpenForWriting()) {
        // Resolve parent folder
        std::string raw_root, item_path;
        Throw(!backend.ParseUrl(path, raw_root, item_path, err) ? err : "");

        std::string root_id;
        if (!cache_.GetRootId(backend.Scheme(), raw_root, root_id)) {
            std::string tok;
            Throw(!GetToken(backend.Scheme(), tok, err) ? "cloudfs: " + err : "");
            Throw(!backend.ResolveRoot(raw_root, tok, root_id, err) ? "cloudfs: " + err : "");
            cache_.PutRootId(backend.Scheme(), raw_root, root_id);
        }

        // Resolve parent
        auto parent_path = item_path.substr(0, item_path.rfind('/'));
        auto file_name = item_path.substr(item_path.rfind('/') + 1);
        if (parent_path.empty())
            parent_path = "/";

        std::string tok;
        Throw(!GetToken(backend.Scheme(), tok, err) ? "cloudfs: " + err : "");

        CloudItem parent;
        Throw(!backend.Stat(root_id, parent_path, tok, parent, err)
                  ? "cloudfs: parent not found: " + parent_path + " — " + err
                  : "");

        auto caps = backend.Capabilities();
        CloudUploadSession session;
        session.chunk_size_bytes = caps.upload_chunk_alignment * 4; // 4× alignment
        session.chunk_size_bytes = std::max(session.chunk_size_bytes, caps.min_upload_chunk);
        session.chunk_size_bytes = std::min(session.chunk_size_bytes, caps.max_upload_chunk);

        Throw(!backend.CreateUploadSession(root_id, parent.id, file_name, -1, tok, session, err)
                  ? "cloudfs: " + err
                  : "");

        CloudItem placeholder;
        placeholder.name = file_name;
        placeholder.size = 0;
        auto h = make_uniq<CloudFileHandle>(*this, path, backend, placeholder, root_id, flags);
        h->upload_session = session;
        h->upload_active = true;
        return std::move(h);
    }

    // Read mode
    CloudItem item;
    std::string root_id;
    Throw(!ResolvePath(backend, path, item, root_id, err) ? "cloudfs: " + err : "");
    Throw(item.is_folder ? "cloudfs: cannot open folder as file: " + path : "");

    auto h = make_uniq<CloudFileHandle>(*this, path, backend, item, root_id, flags);

    std::string tok;
    if (GetToken(backend.Scheme(), tok, err))
        EnsureDownloadUrl(backend, root_id, h->item, tok, err);

    return std::move(h);
}

// ─── Read ─────────────────────────────────────────────────────────────────────
void CloudFileSystem::Read(FileHandle& handle, void* buf, int64_t nr, idx_t loc) {
    auto& h = dynamic_cast<CloudFileHandle&>(handle);
    std::string err, tok;
    GetToken(h.backend.Scheme(), tok, err);

    auto try_read = [&]() -> int64_t {
        if (!h.item.download_url.empty())
            return http_.ReadRange(h.item.download_url, tok, (int64_t)loc, nr,
                                   static_cast<char*>(buf), err);
        return h.backend.ReadRange(h.item, h.root_id, tok, (int64_t)loc, nr,
                                   static_cast<char*>(buf), err);
    };

    int64_t n = try_read();
    if (n < 0) {
        // Evict stale URL and retry once
        cache_.PutDownloadUrl(h.backend.Scheme(), h.item.id, "");
        h.item.download_url.clear();
        if (GetToken(h.backend.Scheme(), tok, err))
            EnsureDownloadUrl(h.backend, h.root_id, h.item, tok, err);
        n = try_read();
        if (n < 0)
            Throw("cloudfs read: " + err);
    }
    h.position = (int64_t)loc + n;
}

int64_t CloudFileSystem::Read(FileHandle& handle, void* buf, int64_t nr) {
    auto& h = dynamic_cast<CloudFileHandle&>(handle);
    int64_t rem = h.file_size - h.position;
    if (rem <= 0)
        return 0;
    int64_t to_read = std::min(nr, rem);
    Read(handle, buf, to_read, (idx_t)h.position);
    return to_read;
}

// ─── Write ────────────────────────────────────────────────────────────────────
static constexpr int64_t kWriteBufMax = 256LL * 1024 * 1024; // 256 MiB max buffer

void CloudFileSystem::Write(FileHandle& handle, void* buf, int64_t nr, idx_t loc) {
    auto& h = dynamic_cast<CloudFileHandle&>(handle);
    Throw(!h.upload_active ? "cloudfs: write on read-only handle" : "");
    if ((int64_t)h.write_buffer.size() < (int64_t)(loc + nr))
        h.write_buffer.resize(loc + nr);
    memcpy(h.write_buffer.data() + loc, buf, nr);
    h.bytes_written = std::max(h.bytes_written, (int64_t)(loc + nr));
}

int64_t CloudFileSystem::Write(FileHandle& handle, void* buf, int64_t nr) {
    auto& h = dynamic_cast<CloudFileHandle&>(handle);
    Write(handle, buf, nr, (idx_t)h.bytes_written);
    return nr;
}

void CloudFileSystem::FlushWrite(CloudFileHandle& h) {
    if (!h.upload_active || h.write_buffer.empty())
        return;
    std::string err, tok;
    Throw(!GetToken(h.backend.Scheme(), tok, err) ? "cloudfs flush: " + err : "");

    auto caps = h.backend.Capabilities();
    int64_t total = h.bytes_written;
    int64_t chunk = h.upload_session.chunk_size_bytes;

    // Align chunk to provider's requirement
    if (caps.upload_chunk_alignment > 1)
        chunk = (chunk / caps.upload_chunk_alignment) * caps.upload_chunk_alignment;
    chunk = std::max(chunk, caps.min_upload_chunk);
    chunk = std::min(chunk, caps.max_upload_chunk);

    for (int64_t offset = 0; offset < total; offset += chunk) {
        int64_t sz = std::min(chunk, total - offset);
        bool last = (offset + sz >= total);
        Throw(!h.backend.UploadChunk(h.upload_session, h.write_buffer.data() + offset, offset, sz,
                                     last, tok, err)
                  ? "cloudfs upload: " + err
                  : "");
    }
    h.upload_active = false;
    h.write_buffer.clear();
    cache_.InvalidateItem(h.backend.Scheme(), h.path);
}

void CloudFileSystem::FileSync(FileHandle& handle) {
    FlushWrite(dynamic_cast<CloudFileHandle&>(handle));
}

// ─── Metadata ─────────────────────────────────────────────────────────────────
int64_t CloudFileSystem::GetFileSize(FileHandle& h) {
    return dynamic_cast<CloudFileHandle&>(h).file_size;
}
timestamp_t CloudFileSystem::GetLastModifiedTime(FileHandle& h) {
    return Timestamp::GetCurrentTimestamp();
}
FileType CloudFileSystem::GetFileType(FileHandle& h) {
    return dynamic_cast<CloudFileHandle&>(h).item.is_folder ? FileType::FILE_TYPE_DIR
                                                            : FileType::FILE_TYPE_REGULAR;
}
void CloudFileSystem::Truncate(FileHandle& h, int64_t sz) {
    dynamic_cast<CloudFileHandle&>(h).declared_size = sz;
}
bool CloudFileSystem::Trim(FileHandle&, idx_t, idx_t) {
    return false;
}

void CloudFileSystem::Seek(FileHandle& h, idx_t loc) {
    dynamic_cast<CloudFileHandle&>(h).position = (int64_t)loc;
}
idx_t CloudFileSystem::SeekPosition(FileHandle& h) {
    return (idx_t) dynamic_cast<CloudFileHandle&>(h).position;
}

// ─── Existence ────────────────────────────────────────────────────────────────
bool CloudFileSystem::FileExists(const string& path, optional_ptr<FileOpener>) {
    auto* b = BackendForNoThrow(path);
    if (!b)
        return false;
    CloudItem item;
    std::string root_id, err;
    if (!ResolvePath(*b, path, item, root_id, err))
        return false;
    return !item.is_folder;
}

bool CloudFileSystem::DirectoryExists(const string& path, optional_ptr<FileOpener>) {
    auto* b = BackendForNoThrow(path);
    if (!b)
        return false;
    CloudItem item;
    std::string root_id, err;
    if (!ResolvePath(*b, path, item, root_id, err))
        return false;
    return item.is_folder;
}

// ─── Directory ops ────────────────────────────────────────────────────────────
void CloudFileSystem::CreateDirectory(const string& path, optional_ptr<FileOpener>) {
    auto& b = BackendFor(path);
    std::string raw_root, item_path, err;
    Throw(!b.ParseUrl(path, raw_root, item_path, err) ? err : "");

    std::string root_id;
    if (!cache_.GetRootId(b.Scheme(), raw_root, root_id)) {
        std::string tok;
        Throw(!GetToken(b.Scheme(), tok, err) ? err : "");
        Throw(!b.ResolveRoot(raw_root, tok, root_id, err) ? err : "");
        cache_.PutRootId(b.Scheme(), raw_root, root_id);
    }
    std::string tok;
    Throw(!GetToken(b.Scheme(), tok, err) ? err : "");

    auto parent_path = item_path.substr(0, item_path.rfind('/'));
    auto name = item_path.substr(item_path.rfind('/') + 1);
    if (parent_path.empty())
        parent_path = "/";

    CloudItem parent;
    Throw(!b.Stat(root_id, parent_path, tok, parent, err) ? "parent not found: " + err : "");

    CloudItem created;
    Throw(!b.CreateFolder(root_id, parent.id, name, tok, created, err) ? err : "");
}

void CloudFileSystem::RemoveDirectory(const string& path, optional_ptr<FileOpener>) {
    auto& b = BackendFor(path);
    CloudItem item;
    std::string root_id, err;
    Throw(!ResolvePath(b, path, item, root_id, err) ? err : "");
    Throw(!item.is_folder ? "not a directory: " + path : "");
    std::string tok;
    Throw(!GetToken(b.Scheme(), tok, err) ? err : "");
    Throw(!b.DeleteItem(root_id, item.id, tok, err) ? err : "");
    cache_.InvalidatePrefix(b.Scheme(), path);
}

void CloudFileSystem::RemoveFile(const string& path, optional_ptr<FileOpener>) {
    auto& b = BackendFor(path);
    CloudItem item;
    std::string root_id, err;
    Throw(!ResolvePath(b, path, item, root_id, err) ? err : "");
    std::string tok;
    Throw(!GetToken(b.Scheme(), tok, err) ? err : "");
    Throw(!b.DeleteItem(root_id, item.id, tok, err) ? err : "");
    cache_.InvalidateItem(b.Scheme(), path);
}

void CloudFileSystem::MoveFile(const string&, const string&, optional_ptr<FileOpener>) {
    Throw("cloudfs: MoveFile not yet implemented");
}

bool CloudFileSystem::ListFiles(const string& dir,
                                const std::function<void(const string&, bool)>& cb, FileOpener*) {
    auto* b = BackendForNoThrow(dir);
    if (!b)
        return false;
    CloudItem folder;
    std::string root_id, err;
    if (!ResolvePath(*b, dir, folder, root_id, err) || !folder.is_folder)
        return false;
    std::string tok;
    if (!GetToken(b->Scheme(), tok, err))
        return false;
    std::string cursor;
    do {
        if (!b->ListFolder(
                root_id, folder.id, tok, [&](const CloudItem& c) { cb(c.name, c.is_folder); },
                cursor, err))
            return false;
    } while (!cursor.empty());
    return true;
}

// ─── Glob ─────────────────────────────────────────────────────────────────────
void CloudFileSystem::GlobRecursive(ICloudBackend& backend, const std::string& root_id,
                                    const std::string& folder_id, const std::string& scheme_prefix,
                                    const std::string& file_pattern, bool recursive,
                                    vector<OpenFileInfo>& results, const std::string& tok) {
    std::string cursor, err;
    do {
        std::vector<CloudItem> batch;
        bool ok = backend.ListFolder(
            root_id, folder_id, tok, [&](const CloudItem& c) { batch.push_back(c); }, cursor, err);
        if (!ok)
            break;
        for (auto& child : batch) {
            std::string child_url = scheme_prefix + "/" + child.name;
            int match = fnmatch(file_pattern.c_str(), child.name.c_str(), 0);
            if (child.is_folder) {
                if (recursive)
                    GlobRecursive(backend, root_id, child.id, child_url, file_pattern, recursive,
                                  results, tok);
            } else {
                if (match == 0)
                    results.emplace_back(child_url);
            }
        }
    } while (!cursor.empty());
}

vector<OpenFileInfo> CloudFileSystem::Glob(const string& path, FileOpener*) {
    vector<OpenFileInfo> results;
    auto* b = BackendForNoThrow(path);
    if (!b)
        return results;

    // No glob chars — just test existence
    bool has_glob = path.find_first_of("*?[") != std::string::npos;
    if (!has_glob) {
        if (FileExists(path))
            results.emplace_back(path);
        return results;
    }

    std::string raw_root, item_path, err;
    if (!b->ParseUrl(path, raw_root, item_path, err))
        return results;

    // Resolve root (with cache)
    std::string root_id;
    if (!cache_.GetRootId(b->Scheme(), raw_root, root_id)) {
        std::string tok;
        if (!GetToken(b->Scheme(), tok, err))
            return results;
        if (!b->ResolveRoot(raw_root, tok, root_id, err))
            return results;
        cache_.PutRootId(b->Scheme(), raw_root, root_id);
    }

    // Get access token
    std::string tok;
    if (!GetToken(b->Scheme(), tok, err))
        return results;

    // Split item_path into static prefix and glob portion
    auto star_pos = item_path.find_first_of("*?[");
    auto slash_before = item_path.rfind('/', star_pos);
    std::string static_prefix =
        (slash_before == std::string::npos) ? "/" : item_path.substr(0, slash_before);
    std::string glob_part =
        (slash_before == std::string::npos) ? item_path : item_path.substr(slash_before + 1);
    bool recursive = glob_part.find("**") != std::string::npos;

    // Resolve starting folder
    CloudItem start_folder;
    if (!b->Stat(root_id, static_prefix, tok, start_folder, err))
        return results;

    // Build the result URL prefix from the original path
    auto star_pos_in_path = path.find_first_of("*?[");
    std::string scheme_url_prefix = path.substr(0, path.rfind('/', star_pos_in_path));

    // Strip leading **/ from file pattern
    std::string file_pat = glob_part;
    while (file_pat.size() >= 3 && file_pat.substr(0, 3) == "**/")
        file_pat = file_pat.substr(3);
    if (file_pat.empty())
        file_pat = "*";

    GlobRecursive(*b, root_id, start_folder.id, scheme_url_prefix, file_pat, recursive, results,
                  tok);

    std::sort(results.begin(), results.end());
    results.erase(std::unique(results.begin(), results.end(),
                              [](const OpenFileInfo& a, const OpenFileInfo& b_) {
                                  return a.path == b_.path;
                              }),
                  results.end());
    return results;
}

} // namespace duckdb
