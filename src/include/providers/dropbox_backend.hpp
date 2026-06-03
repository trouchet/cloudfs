#pragma once
#include "../core/cloud_backend.hpp"
#include "../core/cloud_http.hpp"

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// DropboxBackend
// Scheme: dbxfs://
// Auth:   OAuth2 PKCE (App Key / App Secret) or static long-lived token
// API:    Dropbox API v2
//
// URL format:
//   dbxfs://path/to/file.parquet                  (user's personal Dropbox)
//   dbxfs://ns:123456/path/to/file.parquet        (namespace / team folder)
//
// Key differences from Microsoft/Google backends:
//   - Dropbox API uses a CONTENT endpoint (content.dropboxapi.com) separate
//     from the metadata endpoint (api.dropboxapi.com).
//   - Range reads via the content endpoint with Range header (HTTP 206) — works.
//   - Upload sessions: two-step.
//       1. upload_session/start  → session_id
//       2. upload_session/append_v2  (repeat)
//       3. upload_session/finish → FileMetadata
//   - Dropbox requires knowing the total size only for the finish call, not start.
//   - No pre-authenticated URLs from the standard API; use /files/get_temporary_link
//     for a 4-hour signed URL. We cache these.
//   - List uses a cursor+continue pattern (/files/list_folder + list_folder/continue).
//   - Paths always start with "/" and are case-insensitive.
// ─────────────────────────────────────────────────────────────────────────────
class DropboxBackend : public ICloudBackend {
public:
    explicit DropboxBackend(CloudHttpClient &http) : http_(http) {}

    std::string Scheme() const override { return "dbxfs"; }
    std::string Name()   const override { return "Dropbox"; }

    ProviderCapabilities Capabilities() const override {
        return {
            .supports_range_reads       = true,
            .supports_resumable_uploads = true,
            .supports_server_side_copy  = true,   // /files/copy_v2
            .supports_recursive_list    = true,   // recursive=true in list_folder
            .needs_total_size_upfront   = false,  // only needed at finish step
            .upload_chunk_alignment     = 1,      // any size accepted
            .min_upload_chunk           = 1,
            .max_upload_chunk           = 150 * 1024 * 1024  // 150 MiB Dropbox limit
        };
    }

    bool ParseUrl(const std::string &url,
                  std::string &out_root, std::string &out_path,
                  std::string &err) const override;

    // For Dropbox, root resolution just normalises the path prefix.
    bool ResolveRoot(const std::string &root, const std::string &token,
                     std::string &out_id, std::string &err) override;

    bool Stat(const std::string &root, const std::string &path,
              const std::string &token, CloudItem &out, std::string &err) override;

    int64_t ReadRange(const CloudItem &item, const std::string &root,
                      const std::string &token,
                      int64_t off, int64_t len, char *buf, std::string &err) override;

    // Uses /files/get_temporary_link → 4h signed URL cached in CloudCache.
    bool RefreshDownloadUrl(const CloudItem &item, const std::string &root,
                             const std::string &token,
                             std::string &out_url, std::string &err) override;

    bool ListFolder(const std::string &root, const std::string &folder_id,
                    const std::string &token,
                    const std::function<void(const CloudItem &)> &cb,
                    std::string &cursor, std::string &err) override;

    // Override with efficient recursive list (single API call with recursive=true)
    bool ListFolderRecursive(const std::string &root, const std::string &folder_id,
                              const std::string &token,
                              const std::function<void(const CloudItem &)> &cb,
                              std::string &err) override;

    bool CreateUploadSession(const std::string &root, const std::string &parent_id,
                              const std::string &name, int64_t total_size,
                              const std::string &token,
                              CloudUploadSession &out, std::string &err) override;

    bool UploadChunk(const CloudUploadSession &s, const char *data,
                     int64_t off, int64_t size, bool last,
                     const std::string &token, std::string &err) override;

    bool DeleteItem(const std::string &root, const std::string &id,
                    const std::string &token, std::string &err) override;

    bool CreateFolder(const std::string &root, const std::string &parent_id,
                      const std::string &name, const std::string &token,
                      CloudItem &out, std::string &err) override;

    bool CopyItem(const std::string &root, const std::string &src_id,
                  const std::string &dst_parent_id, const std::string &dst_name,
                  const std::string &token, std::string &err) override;

private:
    CloudItem ParseMetadata(const std::string &json) const;

    static constexpr const char *kApiBase     = "https://api.dropboxapi.com/2";
    static constexpr const char *kContentBase = "https://content.dropboxapi.com/2";

    CloudHttpClient &http_;
};

// ─────────────────────────────────────────────────────────────────────────────
// DropboxOAuthProvider
// PKCE-based OAuth2 for Dropbox (short-lived tokens + refresh tokens).
// ─────────────────────────────────────────────────────────────────────────────
class DropboxOAuthProvider : public OAuth2AuthBase {
public:
    DropboxOAuthProvider(std::string app_key, std::string app_secret)
        : OAuth2AuthBase("dropbox"),
          app_key_(std::move(app_key)),
          app_secret_(std::move(app_secret)) {}

protected:
    bool AcquireToken(std::string &err) override;
    bool RefreshToken(std::string &err) override;

private:
    std::string app_key_, app_secret_;
    static constexpr const char *kTokenUrl  =
        "https://api.dropboxapi.com/oauth2/token";
    static constexpr const char *kDeviceUrl =
        "https://api.dropboxapi.com/oauth2/authorize";
};

} // namespace duckdb
