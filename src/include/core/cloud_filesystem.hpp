#pragma once
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/open_file_info.hpp"

#include "cloud_auth.hpp"
#include "cloud_backend.hpp"
#include "cloud_cache.hpp"
#include "cloud_http.hpp"

#include <memory>
#include <mutex>

namespace duckdb {

class CloudFileHandle;

// ─────────────────────────────────────────────────────────────────────────────
// CloudFileSystem
//
// Single DuckDB FileSystem subclass that drives ALL cloud storage backends.
// It is registered once with DuckDB's VirtualFileSystem and delegates every
// operation to whichever ICloudBackend matches the URL scheme.
//
// To add a new provider:
//   1. Write a class that inherits ICloudBackend.
//   2. Call CloudFileSystem::RegisterBackend(make_uniq<MyBackend>()).
//   3. Done — no changes needed here.
// ─────────────────────────────────────────────────────────────────────────────
class CloudFileSystem : public FileSystem {
  public:
    CloudFileSystem();
    ~CloudFileSystem() override;

    // ── Backend registry ──────────────────────────────────────────────────────

    // Register a new provider. Thread-safe; can be called after Load().
    void RegisterBackend(unique_ptr<ICloudBackend> backend);

    // Attach an auth provider to a scheme (called from CREATE SECRET handler).
    void SetAuth(const std::string& scheme, std::shared_ptr<ICloudAuthProvider> auth);

    // Retrieve auth for a scheme (for diagnostics / secret serialization).
    std::shared_ptr<ICloudAuthProvider> GetAuth(const std::string& scheme) const;

    // List all registered scheme names.
    std::vector<std::string> RegisteredSchemes() const;

    // ── Cache access (for utility SQL functions) ──────────────────────────────
    CloudCache& GetCache() { return cache_; }
    CloudHttpClient& GetHttpClient() { return http_; }

    // ── Public helpers for table functions ──────────────────────────────
    // Find backend for a URL without throwing
    ICloudBackend* BackendForPublic(const std::string& url) { return BackendForNoThrow(url); }
    // Resolve root_id for a (backend, raw_root) pair, caching the result
    bool GetRootIdPublic(ICloudBackend* b, const std::string& raw_root, std::string& out_token,
                         std::string& out_root_id) {
        std::string err;
        if (cache_.GetRootId(b->Scheme(), raw_root, out_root_id)) {
            GetTokenPublic(b->Scheme(), out_token, err);
            return true;
        }
        if (!GetToken(b->Scheme(), out_token, err))
            return false;
        if (!b->ResolveRoot(raw_root, out_token, out_root_id, err))
            return false;
        cache_.PutRootId(b->Scheme(), raw_root, out_root_id);
        return true;
    }
    bool GetTokenPublic(const std::string& scheme, std::string& out_token, std::string& err) {
        return GetToken(scheme, out_token, err);
    }

    // ── DuckDB FileSystem interface ───────────────────────────────────────────

    unique_ptr<FileHandle> OpenFile(const string& path, FileOpenFlags flags,
                                    optional_ptr<FileOpener> opener = nullptr) override;

    void Read(FileHandle& h, void* buf, int64_t n, idx_t loc) override;
    void Write(FileHandle& h, void* buf, int64_t n, idx_t loc) override;
    int64_t Read(FileHandle& h, void* buf, int64_t n) override;
    int64_t Write(FileHandle& h, void* buf, int64_t n) override;

    int64_t GetFileSize(FileHandle& h) override;
    timestamp_t GetLastModifiedTime(FileHandle& h) override;
    FileType GetFileType(FileHandle& h) override;
    void Truncate(FileHandle& h, int64_t size) override;
    bool Trim(FileHandle& h, idx_t off, idx_t len) override;
    void FileSync(FileHandle& h) override;

    void CreateDirectory(const string& dir, optional_ptr<FileOpener> opener = nullptr) override;
    void RemoveDirectory(const string& dir, optional_ptr<FileOpener> opener = nullptr) override;
    bool DirectoryExists(const string& dir, optional_ptr<FileOpener> opener = nullptr) override;
    bool ListFiles(const string& dir, const std::function<void(const string&, bool)>& cb,
                   FileOpener* opener = nullptr) override;
    void MoveFile(const string& src, const string& dst,
                  optional_ptr<FileOpener> opener = nullptr) override;
    void RemoveFile(const string& f, optional_ptr<FileOpener> opener = nullptr) override;
    bool FileExists(const string& f, optional_ptr<FileOpener> opener = nullptr) override;

    vector<OpenFileInfo> Glob(const string& path, FileOpener* opener = nullptr) override;

    bool CanHandleFile(const string& path) override;
    bool CanSeek() override { return true; }
    bool OnDiskFile(FileHandle&) override { return false; }
    void Seek(FileHandle& h, idx_t loc) override;
    idx_t SeekPosition(FileHandle& h) override;
    string GetName() const override { return "CloudFileSystem"; }
    string PathSeparator(const string&) override { return "/"; }

  private:
    // Find the right backend for a URL (throws if none found)
    ICloudBackend& BackendFor(const std::string& path);
    ICloudBackend* BackendForNoThrow(const std::string& path);

    // Get access token for the scheme in a URL
    bool GetToken(const std::string& scheme, std::string& out_token, std::string& err);

    // Resolve path → CloudItem (with cache)
    bool ResolvePath(ICloudBackend& backend, const std::string& url, CloudItem& out_item,
                     std::string& out_root_id, std::string& err);

    // Ensure download URL is fresh for a CloudItem
    bool EnsureDownloadUrl(ICloudBackend& backend, const std::string& root_id, CloudItem& item,
                           const std::string& token, std::string& err);

    // Flush write buffer via upload session
    void FlushWrite(CloudFileHandle& h);

    // Glob recursive helper
    void GlobRecursive(ICloudBackend& backend, const std::string& root_id,
                       const std::string& folder_id, const std::string& scheme_prefix,
                       const std::string& file_pattern, bool recursive,
                       vector<OpenFileInfo>& results, const std::string& token);

    std::unordered_map<std::string, unique_ptr<ICloudBackend>> backends_;
    std::unordered_map<std::string, std::shared_ptr<ICloudAuthProvider>> auth_map_;
    CloudCache cache_;
    CloudHttpClient http_; // shared HTTP client (all backends may use it)
    mutable std::mutex mu_;
};

// ─────────────────────────────────────────────────────────────────────────────
// CloudFileHandle
// ─────────────────────────────────────────────────────────────────────────────
class CloudFileHandle : public FileHandle {
  public:
    CloudFileHandle(CloudFileSystem& fs, const std::string& path, ICloudBackend& backend,
                    const CloudItem& item, const std::string& root_id, FileOpenFlags flags);
    ~CloudFileHandle() override;
    void Close() override;

    // Read state
    int64_t file_size = 0;
    int64_t position = 0;
    std::string root_id;      // resolved root/drive/bucket ID
    std::string download_url; // pre-authenticated (may be empty)
    CloudItem item;
    FileOpenFlags open_flags;

    // Write state
    CloudUploadSession upload_session;
    std::vector<char> write_buffer;
    int64_t bytes_written = 0;
    int64_t declared_size = -1;
    bool upload_active = false;

    // Back-reference to the backend that owns this handle
    ICloudBackend& backend;
};

} // namespace duckdb
