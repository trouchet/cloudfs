#pragma once
#include "cloud_item.hpp"
#include "cloud_auth.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// ProviderCapabilities
// Declares what a backend supports so CloudFileSystem can adapt.
// ─────────────────────────────────────────────────────────────────────────────
struct ProviderCapabilities {
    bool supports_range_reads       = true;  // HTTP 206 Partial Content
    bool supports_resumable_uploads = true;  // chunked upload sessions
    bool supports_server_side_copy  = false; // atomic copy without re-upload
    bool supports_recursive_list    = false; // single API call for deep listing
    bool needs_total_size_upfront   = false; // upload requires Content-Length at start
    int64_t upload_chunk_alignment  = 1;     // bytes (e.g. 320*1024 for SharePoint)
    int64_t min_upload_chunk        = 1;
    int64_t max_upload_chunk        = 100 * 1024 * 1024; // 100 MiB default
};

// ─────────────────────────────────────────────────────────────────────────────
// ICloudBackend
//
// THE central abstraction.  Adding a new cloud storage provider means
// writing one class that implements this interface.  The CloudFileSystem
// (which handles DuckDB's FileSystem contract) never changes.
//
// Design principle: every method is stateless w.r.t. tokens — the backend
// receives an access_token on each call so auth lifecycle is owned by
// ICloudAuthProvider, not here.
// ─────────────────────────────────────────────────────────────────────────────
class ICloudBackend {
public:
    virtual ~ICloudBackend() = default;

    // ── Identity ──────────────────────────────────────────────────────────────

    // Protocol scheme this backend claims (e.g. "spfs", "gdfs", "dbxfs").
    virtual std::string Scheme() const = 0;

    // Human-readable provider name for error messages.
    virtual std::string Name()   const = 0;

    // What this backend can and cannot do.
    virtual ProviderCapabilities Capabilities() const = 0;

    // ── URL parsing ───────────────────────────────────────────────────────────

    // Returns true if this backend should handle the given path.
    // Default: match on Scheme() prefix.
    virtual bool CanHandle(const std::string &path) const {
        const std::string pfx = Scheme() + "://";
        return path.size() > pfx.size() && path.substr(0, pfx.size()) == pfx;
    }

    // Parse a scheme://... URL into opaque "address" components.
    // out_root:    identifies the storage root (bucket, drive, site)
    // out_path:    the file/folder path within that root
    virtual bool ParseUrl(const std::string &url,
                          std::string &out_root,
                          std::string &out_path,
                          std::string &err) const = 0;

    // ── Item resolution ───────────────────────────────────────────────────────

    // Resolve a (root, path) pair to a CloudItem.
    // Called by OpenFile(), FileExists(), GetFileSize(), etc.
    virtual bool Stat(const std::string &root,
                      const std::string &path,
                      const std::string &access_token,
                      CloudItem         &out_item,
                      std::string       &err) = 0;

    // ── Read ──────────────────────────────────────────────────────────────────

    // Read bytes [offset, offset+length) into buffer.
    // Must honour ProviderCapabilities::supports_range_reads.
    // Returns bytes read, or -1 on error.
    virtual int64_t ReadRange(const CloudItem   &item,
                               const std::string &root,
                               const std::string &access_token,
                               int64_t            offset,
                               int64_t            length,
                               char              *out_buffer,
                               std::string       &err) = 0;

    // Refresh a pre-authenticated download URL (called when cached URL expires).
    // Providers without pre-auth URLs (GDrive) can return false; the framework
    // will fall back to ReadRange() via the API directly.
    virtual bool RefreshDownloadUrl(const CloudItem   &item,
                                     const std::string &root,
                                     const std::string &access_token,
                                     std::string       &out_url,
                                     std::string       &err) {
        err = Name() + ": pre-authenticated download URLs not supported";
        return false;
    }

    // ── List / Glob ───────────────────────────────────────────────────────────

    // List immediate children of a folder.
    // Callback receives (item, is_folder).
    // next_cursor: pass back for pagination; empty = done.
    virtual bool ListFolder(const std::string &root,
                             const std::string &folder_id,
                             const std::string &access_token,
                             const std::function<void(const CloudItem &)> &callback,
                             std::string &next_cursor,
                             std::string &err) = 0;

    // Optional: providers with recursive-list APIs can override this for efficiency.
    // Default implementation calls ListFolder() recursively.
    virtual bool ListFolderRecursive(const std::string &root,
                                      const std::string &folder_id,
                                      const std::string &access_token,
                                      const std::function<void(const CloudItem &)> &callback,
                                      std::string &err);

    // ── Write ─────────────────────────────────────────────────────────────────

    // Begin a resumable upload session.
    // out_session.upload_url is valid until CompleteUpload().
    virtual bool CreateUploadSession(const std::string      &root,
                                      const std::string      &parent_id,
                                      const std::string      &file_name,
                                      int64_t                 total_size,  // -1 = unknown
                                      const std::string      &access_token,
                                      CloudUploadSession     &out_session,
                                      std::string            &err) = 0;

    // Upload one chunk.  last_chunk=true signals EOF.
    virtual bool UploadChunk(const CloudUploadSession &session,
                              const char              *data,
                              int64_t                  chunk_offset,
                              int64_t                  chunk_size,
                              bool                     last_chunk,
                              const std::string       &access_token,
                              std::string             &err) = 0;

    // Abort an upload session (cleanup).
    virtual bool AbortUpload(const CloudUploadSession &session,
                              const std::string       &access_token,
                              std::string             &err) { return true; }

    // ── Mutations ─────────────────────────────────────────────────────────────

    virtual bool DeleteItem(const std::string &root,
                             const std::string &item_id,
                             const std::string &access_token,
                             std::string       &err) = 0;

    virtual bool CreateFolder(const std::string &root,
                               const std::string &parent_id,
                               const std::string &folder_name,
                               const std::string &access_token,
                               CloudItem         &out_item,
                               std::string       &err) = 0;

    // Optional server-side copy (avoids re-upload for large files).
    virtual bool CopyItem(const std::string &root,
                           const std::string &src_item_id,
                           const std::string &dst_parent_id,
                           const std::string &dst_name,
                           const std::string &access_token,
                           std::string       &err) {
        err = Name() + ": server-side copy not supported";
        return false;
    }

    // ── Root resolution ───────────────────────────────────────────────────────

    // Resolve the root string (parsed by ParseUrl) to an internal drive/bucket ID.
    // The result is cached by CloudCache.
    // For providers where root == bucket name (S3, Dropbox) this is a no-op.
    virtual bool ResolveRoot(const std::string &root,
                              const std::string &access_token,
                              std::string       &out_root_id,
                              std::string       &err) {
        out_root_id = root;
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ICloudBackend default implementations
// ─────────────────────────────────────────────────────────────────────────────
inline bool ICloudBackend::ListFolderRecursive(
    const std::string &root,
    const std::string &folder_id,
    const std::string &access_token,
    const std::function<void(const CloudItem &)> &callback,
    std::string &err)
{
    std::string cursor;
    do {
        std::vector<CloudItem> batch;
        if (!ListFolder(root, folder_id, access_token,
                        [&](const CloudItem &item) { batch.push_back(item); },
                        cursor, err)) return false;
        for (auto &item : batch) {
            callback(item);
            if (item.is_folder) {
                if (!ListFolderRecursive(root, item.id, access_token, callback, err))
                    return false;
            }
        }
    } while (!cursor.empty());
    return true;
}

} // namespace duckdb
