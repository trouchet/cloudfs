#include "core/cloud_filesystem.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_opener.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/uuid.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>

// Portable glob wildcard match (replaces POSIX fnmatch for Windows compat).
// Supports '*' (any sequence) and '?' (any single char). Returns 0 on match.
static int glob_match(const char* pattern, const char* name) {
    while (*pattern && *name) {
        if (*pattern == '*') {
            while (*pattern == '*')
                ++pattern;
            if (!*pattern)
                return 0;
            while (*name)
                if (glob_match(pattern, name++) == 0)
                    return 0;
            return 1;
        }
        if (*pattern != '?' && *pattern != *name)
            return 1;
        ++pattern;
        ++name;
    }
    while (*pattern == '*')
        ++pattern;
    return (*pattern || *name) ? 1 : 0;
}

namespace duckdb {

static void Throw(const std::string& msg) {
    if (!msg.empty())
        throw IOException(msg);
}

// A lookup failed.  "Does not exist" is a normal answer for existence checks
// and globs; anything else (auth, network, permissions) must not be reported
// as "no such file", so it is raised.
static void ThrowUnlessNotFound(const std::string& url, const std::string& err) {
    if (CloudError::IsNotFound(err))
        return;
    throw IOException("cloudfs: cannot access '" + url + "': " + err);
}

// ─── CloudFileHandle ──────────────────────────────────────────────────────────
CloudFileHandle::CloudFileHandle(CloudFileSystem& fs, const std::string& path,
                                 ICloudBackend& backend_, const CloudItem& item_,
                                 const std::string& root_id_, FileOpenFlags flags)
    : FileHandle(fs, path, flags), file_size(item_.size), root_id(root_id_), item(item_),
      open_flags(flags), backend(backend_) {}

CloudFileHandle::~CloudFileHandle() {
    if (!upload_active)
        return;
    // Reached without an explicit Close().  A destructor must not throw, so
    // the upload is completed only on the normal path and any failure turns
    // into an abort; during stack unwinding the write was interrupted and the
    // partial upload is discarded.
    auto& cfs = dynamic_cast<CloudFileSystem&>(file_system);
    if (std::uncaught_exceptions() > 0) {
        cfs.AbortUpload(*this);
        return;
    }
    try {
        cfs.FinishUpload(*this);
    } catch (...) {
        // Nobody is left to report the failure to; do not leave a session behind.
        cfs.AbortUpload(*this);
    }
}

void CloudFileHandle::Close() {
    if (upload_active)
        dynamic_cast<CloudFileSystem&>(file_system).FinishUpload(*this);
}

// ─── CloudFileSystem ──────────────────────────────────────────────────────────
CloudFileSystem::CloudFileSystem(DatabaseInstance& db) : secrets_(db) {}
CloudFileSystem::~CloudFileSystem() = default;

void CloudFileSystem::RegisterBackend(unique_ptr<ICloudBackend> b) {
    std::lock_guard<std::mutex> lk(mu_);
    backends_[b->Scheme()] = std::move(b);
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

bool CloudFileSystem::GetToken(const std::string& url, std::string& out, std::string& err) {
    auto* backend = BackendForNoThrow(url);
    if (!backend) {
        err = "cloudfs: no backend registered for URL: " + url;
        return false;
    }
    std::shared_ptr<ICloudAuthProvider> auth;
    if (!secrets_.ResolveAuth(url, backend->SecretType(), auth, err))
        return false;
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
        if (!GetToken(url, tok, err))
            return false;
        if (!backend.ResolveRoot(raw_root, tok, root_id, err))
            return false;
        cache_.PutRootId(backend.Scheme(), raw_root, root_id);
    }
    out_root_id = root_id;

    // 4. Stat the item
    std::string tok;
    if (!GetToken(url, tok, err))
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
                                                 optional_ptr<FileOpener> opener) {
    auto& backend = BackendFor(path);
    std::string err;

    if (flags.OpenForWriting()) {
        // Resolve parent folder
        std::string raw_root, item_path;
        Throw(!backend.ParseUrl(path, raw_root, item_path, err) ? err : "");

        std::string root_id;
        if (!cache_.GetRootId(backend.Scheme(), raw_root, root_id)) {
            std::string tok;
            Throw(!GetToken(path, tok, err) ? "cloudfs: " + err : "");
            Throw(!backend.ResolveRoot(raw_root, tok, root_id, err) ? "cloudfs: " + err : "");
            cache_.PutRootId(backend.Scheme(), raw_root, root_id);
        }

        // Resolve parent
        auto parent_path = item_path.substr(0, item_path.rfind('/'));
        auto file_name = item_path.substr(item_path.rfind('/') + 1);
        if (parent_path.empty())
            parent_path = "/";

        std::string tok;
        Throw(!GetToken(path, tok, err) ? "cloudfs: " + err : "");

        CloudItem parent;
        Throw(!backend.Stat(root_id, parent_path, tok, parent, err)
                  ? "cloudfs: parent not found: " + parent_path + " — " + err
                  : "");

        CloudItem placeholder;
        placeholder.name = file_name;
        placeholder.size = 0;
        auto h = make_uniq<CloudFileHandle>(*this, path, backend, placeholder, root_id, flags);
        h->upload_active = true;
        h->upload_parent_id = parent.id;
        h->upload_file_name = file_name;

        auto caps = backend.Capabilities();
        h->spooling = caps.needs_total_size_upfront;
        if (h->spooling) {
            // Prefer DuckDB's temp_directory; fall back to the system one.
            Value temp_dir;
            if (FileOpener::TryGetCurrentSetting(opener, "temp_directory", temp_dir) &&
                !temp_dir.IsNull() && !temp_dir.ToString().empty())
                h->spool_dir = temp_dir.ToString();
        }
        return std::move(h);
    }

    // Read mode
    CloudItem item;
    std::string root_id;
    Throw(!ResolvePath(backend, path, item, root_id, err) ? "cloudfs: " + err : "");
    Throw(item.is_folder ? "cloudfs: cannot open folder as file: " + path : "");

    auto h = make_uniq<CloudFileHandle>(*this, path, backend, item, root_id, flags);

    std::string tok;
    if (GetToken(path, tok, err))
        EnsureDownloadUrl(backend, root_id, h->item, tok, err);

    return std::move(h);
}

// ─── Read ─────────────────────────────────────────────────────────────────────
void CloudFileSystem::Read(FileHandle& handle, void* buf, int64_t nr, idx_t loc) {
    auto& h = dynamic_cast<CloudFileHandle&>(handle);
    std::string err, tok;
    Throw(!GetToken(h.path, tok, err) ? "cloudfs read: " + err : "");

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
        if (GetToken(h.path, tok, err))
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
// Chunk size for non-final chunks: what the backend asked for in its session,
// rounded down to its alignment and clamped to its limits.
static int64_t AlignedChunkSize(const ProviderCapabilities& caps, int64_t requested) {
    int64_t chunk = requested > 0 ? requested : caps.upload_chunk_alignment * 4;
    chunk = std::max(chunk, caps.min_upload_chunk);
    chunk = std::min(chunk, caps.max_upload_chunk);
    if (caps.upload_chunk_alignment > 1) {
        chunk = (chunk / caps.upload_chunk_alignment) * caps.upload_chunk_alignment;
        chunk = std::max(chunk, caps.upload_chunk_alignment);
    }
    return std::max<int64_t>(chunk, 1);
}

// Default chunk size before the session exists (the backend may refine it).
static constexpr int64_t kDefaultChunk = 8LL * 1024 * 1024;

void CloudFileSystem::Write(FileHandle& handle, void* buf, int64_t nr, idx_t loc) {
    auto& h = dynamic_cast<CloudFileHandle&>(handle);
    Throw(!h.upload_active ? "cloudfs: write on a handle that is not open for writing" : "");
    // Upload sessions are append-only: there is no way to rewrite bytes that
    // were already sent, so only sequential writes can be supported.
    if ((int64_t)loc != h.bytes_written)
        throw NotImplementedException(
            "cloudfs: non-sequential write to '%s' (offset %llu, expected %lld); cloud uploads "
            "are append-only",
            h.path, (unsigned long long)loc, (long long)h.bytes_written);
    if (nr <= 0)
        return;

    if (h.spooling) {
        if (!h.spool_file) {
            h.spool_fs = FileSystem::CreateLocal();
            std::string dir = h.spool_dir;
            if (dir.empty())
                dir = std::filesystem::temp_directory_path().string();
            else if (!h.spool_fs->DirectoryExists(dir))
                h.spool_fs->CreateDirectory(dir);
            h.spool_path = h.spool_fs->JoinPath(
                dir, "cloudfs_upload_" + UUID::ToString(UUID::GenerateRandomUUID()) + ".tmp");
            h.spool_file = h.spool_fs->OpenFile(
                h.spool_path, FileFlags::FILE_FLAGS_WRITE | FileFlags::FILE_FLAGS_READ |
                                  FileFlags::FILE_FLAGS_FILE_CREATE_NEW);
        }
        h.spool_fs->Write(*h.spool_file, buf, nr, (idx_t)h.bytes_written);
        h.bytes_written += nr;
        h.position = h.bytes_written;
        return;
    }

    auto* src = static_cast<const char*>(buf);
    h.write_buffer.insert(h.write_buffer.end(), src, src + nr);
    h.bytes_written += nr;
    h.position = h.bytes_written;
    UploadFullChunks(h);
}

int64_t CloudFileSystem::Write(FileHandle& handle, void* buf, int64_t nr) {
    auto& h = dynamic_cast<CloudFileHandle&>(handle);
    Write(handle, buf, nr, (idx_t)h.bytes_written);
    return nr;
}

void CloudFileSystem::EnsureUploadSession(CloudFileHandle& h, int64_t total) {
    if (h.session_created)
        return;
    std::string err, tok;
    Throw(!GetToken(h.path, tok, err) ? "cloudfs upload: " + err : "");
    h.upload_session.total_size_bytes = total;
    Throw(!h.backend.CreateUploadSession(h.root_id, h.upload_parent_id, h.upload_file_name, total,
                                         tok, h.upload_session, err)
              ? "cloudfs upload: " + err
              : "");
    h.session_created = true;
    // The backend states its preferred chunk size in the session.
    h.chunk_size = AlignedChunkSize(h.backend.Capabilities(), h.upload_session.chunk_size_bytes);
}

void CloudFileSystem::UploadChunk(CloudFileHandle& h, const char* data, int64_t size, bool last) {
    // The total is known up front only when spooling or for a single-chunk file.
    EnsureUploadSession(h, (h.spooling || last) ? h.bytes_written : -1);
    // Backends that put the total in each request read it from the session.
    if (h.spooling || last)
        h.upload_session.total_size_bytes = h.bytes_written;

    std::string err, tok;
    // Fetch the token per chunk: long uploads outlive an access token.
    Throw(!GetToken(h.path, tok, err) ? "cloudfs upload: " + err : "");
    if (!h.backend.UploadChunk(h.upload_session, data, h.bytes_uploaded, size, last, tok, err))
        Throw("cloudfs upload: " + (err.empty() ? std::string("chunk rejected") : err));
    h.bytes_uploaded += size;
}

void CloudFileSystem::UploadFullChunks(CloudFileHandle& h) {
    if (h.chunk_size <= 0)
        h.chunk_size = AlignedChunkSize(h.backend.Capabilities(), kDefaultChunk);
    if ((int64_t)h.write_buffer.size() <= h.chunk_size)
        return;
    // More than one chunk's worth is buffered, so the file needs a session
    // with an open-ended total; creating it also settles the chunk size.
    EnsureUploadSession(h, -1);

    size_t consumed = 0;
    // Strictly greater: the last byte(s) stay buffered until FinishUpload(),
    // because only then do we know which chunk is the final one.
    while ((int64_t)(h.write_buffer.size() - consumed) > h.chunk_size) {
        UploadChunk(h, h.write_buffer.data() + consumed, h.chunk_size, false);
        consumed += (size_t)h.chunk_size;
    }
    if (consumed > 0)
        h.write_buffer.erase(h.write_buffer.begin(), h.write_buffer.begin() + (ptrdiff_t)consumed);
}

void CloudFileSystem::FinishUpload(CloudFileHandle& h) {
    if (!h.upload_active)
        return;
    try {
        if (h.spooling) {
            const int64_t total = h.bytes_written;
            EnsureUploadSession(h, total); // also settles h.chunk_size
            std::vector<char> chunk;
            do {
                const int64_t want = std::min(h.chunk_size, total - h.bytes_uploaded);
                chunk.resize((size_t)want);
                if (want > 0)
                    h.spool_file->Read(chunk.data(), (idx_t)want, (idx_t)h.bytes_uploaded);
                const bool last = h.bytes_uploaded + want >= total;
                UploadChunk(h, chunk.data(), want, last);
            } while (h.bytes_uploaded < total);
        } else {
            UploadFullChunks(h);
            // Whatever is left (possibly nothing, for an empty file) is the final chunk.
            UploadChunk(h, h.write_buffer.data(), (int64_t)h.write_buffer.size(), true);
        }
    } catch (...) {
        AbortUpload(h);
        throw;
    }

    h.upload_active = false;
    h.write_buffer.clear();
    h.write_buffer.shrink_to_fit();
    h.file_size = h.bytes_written;
    if (h.spool_file) {
        h.spool_file.reset();
        try {
            h.spool_fs->RemoveFile(h.spool_path);
        } catch (...) {
            // leftover temp file: not worth failing a completed upload
        }
    }
    cache_.InvalidateItem(h.backend.Scheme(), h.path);
}

void CloudFileSystem::AbortUpload(CloudFileHandle& h) noexcept {
    if (!h.upload_active)
        return;
    h.upload_active = false;
    try {
        if (h.session_created) {
            std::string err, tok;
            if (GetToken(h.path, tok, err))
                h.backend.AbortUpload(h.upload_session, tok, err);
        }
    } catch (...) {}
    try {
        h.write_buffer.clear();
        if (h.spool_file) {
            h.spool_file.reset();
            h.spool_fs->RemoveFile(h.spool_path);
        }
    } catch (...) {}
    cache_.InvalidateItem(h.backend.Scheme(), h.path);
}

// A resumable upload cannot be made durable part-way: the object only exists
// once the session is completed, which happens on Close().  Full chunks are
// already on their way, so there is nothing more to do here.
void CloudFileSystem::FileSync(FileHandle&) {}

// ─── Metadata ─────────────────────────────────────────────────────────────────
int64_t CloudFileSystem::GetFileSize(FileHandle& h) {
    auto& cfh = dynamic_cast<CloudFileHandle&>(h);
    return cfh.upload_active ? cfh.bytes_written : cfh.file_size;
}
timestamp_t CloudFileSystem::GetLastModifiedTime(FileHandle& h) {
    auto& cfh = dynamic_cast<CloudFileHandle&>(h);
    if (cfh.item.modified_time_ms > 0) {
        // Convert Unix milliseconds to DuckDB timestamp
        return Timestamp::FromEpochMs(cfh.item.modified_time_ms);
    }
    // Fallback: if no modification time available, use current time
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
    if (!ResolvePath(*b, path, item, root_id, err)) {
        ThrowUnlessNotFound(path, err);
        return false;
    }
    return !item.is_folder;
}

bool CloudFileSystem::DirectoryExists(const string& path, optional_ptr<FileOpener>) {
    auto* b = BackendForNoThrow(path);
    if (!b)
        return false;
    CloudItem item;
    std::string root_id, err;
    if (!ResolvePath(*b, path, item, root_id, err)) {
        ThrowUnlessNotFound(path, err);
        return false;
    }
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
        Throw(!GetToken(path, tok, err) ? err : "");
        Throw(!b.ResolveRoot(raw_root, tok, root_id, err) ? err : "");
        cache_.PutRootId(b.Scheme(), raw_root, root_id);
    }
    std::string tok;
    Throw(!GetToken(path, tok, err) ? err : "");

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
    Throw(!GetToken(path, tok, err) ? err : "");
    Throw(!b.DeleteItem(root_id, item.id, tok, err) ? err : "");
    cache_.InvalidatePrefix(b.Scheme(), path);
}

void CloudFileSystem::RemoveFile(const string& path, optional_ptr<FileOpener>) {
    auto& b = BackendFor(path);
    CloudItem item;
    std::string root_id, err;
    Throw(!ResolvePath(b, path, item, root_id, err) ? err : "");
    std::string tok;
    Throw(!GetToken(path, tok, err) ? err : "");
    Throw(!b.DeleteItem(root_id, item.id, tok, err) ? err : "");
    cache_.InvalidateItem(b.Scheme(), path);
}

void CloudFileSystem::MoveFile(const string& src_url, const string& dst_url,
                               optional_ptr<FileOpener>) {
    auto& b = BackendFor(src_url);
    if (!b.CanHandle(dst_url))
        Throw("cloudfs: move across providers is not supported");

    std::string src_raw, src_path, dst_raw, dst_path, err;
    Throw(!b.ParseUrl(src_url, src_raw, src_path, err) ? err : "");
    Throw(!b.ParseUrl(dst_url, dst_raw, dst_path, err) ? err : "");
    if (src_raw != dst_raw)
        Throw("cloudfs: cross-root move not supported; source and destination must share the same "
              "storage root");

    // Resolve root (with cache)
    std::string root_id;
    if (!cache_.GetRootId(b.Scheme(), src_raw, root_id)) {
        std::string tok;
        Throw(!GetToken(src_url, tok, err) ? err : "");
        Throw(!b.ResolveRoot(src_raw, tok, root_id, err) ? err : "");
        cache_.PutRootId(b.Scheme(), src_raw, root_id);
    }
    std::string tok;
    Throw(!GetToken(src_url, tok, err) ? err : "");

    // Stat the source item
    CloudItem src_item;
    Throw(!b.Stat(root_id, src_path, tok, src_item, err) ? "source not found: " + err : "");

    // Decompose destination into parent path + filename
    auto last_slash = dst_path.rfind('/');
    std::string dst_name =
        (last_slash == std::string::npos) ? dst_path : dst_path.substr(last_slash + 1);
    Throw(dst_name.empty() ? "cloudfs: destination must include a filename" : "");
    std::string dst_parent_path =
        (last_slash == std::string::npos || last_slash == 0) ? "/" : dst_path.substr(0, last_slash);

    if (dst_name.empty())
        Throw("cloudfs: destination path must include a filename, not a directory path");

    // Stat the destination parent folder
    CloudItem dst_parent;
    Throw(!b.Stat(root_id, dst_parent_path, tok, dst_parent, err)
              ? "destination parent not found: " + err
              : "");

    // Perform server-side move
    Throw(!b.MoveItem(root_id, src_item, dst_parent, dst_name, tok, err) ? err : "");

    // Cached metadata is keyed by URL.  Both ends are stale now: the source is
    // gone and the destination may have replaced an item of a different size.
    // The prefix form also covers everything beneath a moved directory.
    cache_.InvalidatePrefix(b.Scheme(), src_url);
    cache_.InvalidatePrefix(b.Scheme(), dst_url);
}

bool CloudFileSystem::ListFiles(const string& dir,
                                const std::function<void(const string&, bool)>& cb, FileOpener*) {
    auto* b = BackendForNoThrow(dir);
    if (!b)
        return false;
    CloudItem folder;
    std::string root_id, err;
    if (!ResolvePath(*b, dir, folder, root_id, err)) {
        ThrowUnlessNotFound(dir, err);
        return false;
    }
    if (!folder.is_folder)
        return false;
    std::string tok;
    if (!GetToken(dir, tok, err))
        throw IOException("cloudfs: cannot list '" + dir + "': " + err);
    // Collect first: a failure on a later page must not leave the caller with
    // a partial listing it already acted on.
    std::vector<std::pair<std::string, bool>> entries;
    std::string cursor;
    do {
        if (!b->ListFolder(
                root_id, folder.id, tok,
                [&](const CloudItem& c) { entries.emplace_back(c.name, c.is_folder); }, cursor,
                err))
            throw IOException("cloudfs: cannot list '" + dir + "': " + err);
    } while (!cursor.empty());
    for (auto& e : entries)
        cb(e.first, e.second);
    return true;
}

// ─── Glob ─────────────────────────────────────────────────────────────────────
void CloudFileSystem::GlobRecursive(ICloudBackend& backend, const std::string& root_id,
                                    const std::string& folder_id, const std::string& scheme_prefix,
                                    const std::string& file_pattern, bool recursive,
                                    vector<OpenFileInfo>& results, const std::string& tok) {
    // Fast path: backend has a native recursive list and items carry a full path.
    // Derive scheme root ("scheme://authority") from scheme_prefix to build result URLs.
    if (recursive && backend.Capabilities().supports_recursive_list) {
        const std::string pfx = backend.Scheme() + "://";
        size_t auth_end = scheme_prefix.find('/', pfx.size());
        std::string scheme_base =
            (auth_end != std::string::npos) ? scheme_prefix.substr(0, auth_end) : scheme_prefix;
        std::string err;
        if (!backend.ListFolderRecursive(
                root_id, folder_id, tok,
                [&](const CloudItem& c) {
                    if (c.is_folder || c.path.empty())
                        return;
                    if (glob_match(file_pattern.c_str(), c.name.c_str()) == 0)
                        results.emplace_back(scheme_base + c.path);
                },
                err))
            throw IOException("cloudfs: cannot list '" + scheme_prefix + "': " + err);
        return;
    }
    // Default path: iterate with ListFolder, recurse manually for subdirectories.
    std::string cursor, err;
    do {
        std::vector<CloudItem> batch;
        bool ok = backend.ListFolder(
            root_id, folder_id, tok, [&](const CloudItem& c) { batch.push_back(c); }, cursor, err);
        if (!ok)
            throw IOException("cloudfs: cannot list '" + scheme_prefix + "': " + err);
        for (auto& child : batch) {
            std::string child_url = scheme_prefix + "/" + child.name;
            int match = glob_match(file_pattern.c_str(), child.name.c_str());
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

    const std::string fail = "cloudfs: cannot expand '" + path + "': ";
    std::string raw_root, item_path, err;
    if (!b->ParseUrl(path, raw_root, item_path, err))
        throw IOException(fail + err);

    // Resolve root (with cache)
    std::string root_id;
    if (!cache_.GetRootId(b->Scheme(), raw_root, root_id)) {
        std::string tok;
        if (!GetToken(path, tok, err))
            throw IOException(fail + err);
        if (!b->ResolveRoot(raw_root, tok, root_id, err))
            throw IOException(fail + err);
        cache_.PutRootId(b->Scheme(), raw_root, root_id);
    }

    // Get access token
    std::string tok;
    if (!GetToken(path, tok, err))
        throw IOException(fail + err);

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
    if (!b->Stat(root_id, static_prefix, tok, start_folder, err)) {
        if (CloudError::IsNotFound(err))
            return results; // the folder does not exist: no matches
        throw IOException(fail + err);
    }

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
