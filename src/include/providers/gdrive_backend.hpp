#pragma once
#include "../core/cloud_backend.hpp"
#include "../core/cloud_http.hpp"

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// GDriveBackend
// Scheme: gdfs://
// Auth:   OAuth2 PKCE (user) or Service Account JWT (server)
// API:    Google Drive API v3
//
// URL format:
//   gdfs://MyDrive/path/to/file.parquet           (personal drive)
//   gdfs://drive/CorpDrive/path/to/file.parquet   (Shared Drive)
//
// Key differences from Microsoft backends:
//   - No pre-authenticated download URLs: reading always goes through
//     the API with alt=media (uses auth header, no expiry issues)
//   - Google Drive does NOT support HTTP range requests on its download API.
//     ** We implement range reads via the X-Goog-Request-Params header
//        and the `alt=media` endpoint with the Range header, which IS
//        supported as of Drive API v3.
//   - Uploads use either simple upload (< 5 MB) or resumable upload sessions.
//   - Folders are just files with MIME type application/vnd.google-apps.folder
//   - Files have a unique `id`, not a path; path resolution is a tree walk.
//   - Shortcut files are transparently followed.
// ─────────────────────────────────────────────────────────────────────────────
class GDriveBackend : public ICloudBackend {
  public:
    explicit GDriveBackend(CloudHttpClient& http) : http_(http) {}

    std::string Scheme() const override { return "gdfs"; }
    std::string Name() const override { return "Google Drive"; }

    ProviderCapabilities Capabilities() const override {
        ProviderCapabilities caps;
        caps.supports_range_reads = true;       // via Range header on alt=media
        caps.supports_resumable_uploads = true; // resumable upload protocol
        caps.supports_server_side_copy = true;  // files.copy API
        caps.supports_recursive_list = false;
        caps.needs_total_size_upfront = false;
        caps.upload_chunk_alignment = 256 * 1024; // 256 KiB (Google requirement)
        caps.min_upload_chunk = 256 * 1024;
        caps.max_upload_chunk = 256 * 1024 * 1024;
        return caps;
    }

    bool ParseUrl(const std::string& url, std::string& out_root, std::string& out_path,
                  std::string& err) const override;

    bool ResolveRoot(const std::string& root, const std::string& token, std::string& out_id,
                     std::string& err) override;

    bool Stat(const std::string& root, const std::string& path, const std::string& token,
              CloudItem& out, std::string& err) override;

    int64_t ReadRange(const CloudItem& item, const std::string& root, const std::string& token,
                      int64_t off, int64_t len, char* buf, std::string& err) override;

    // GDrive does not issue pre-authenticated URLs (RefreshDownloadUrl returns false).
    // All reads go through the authenticated alt=media endpoint.
    bool RefreshDownloadUrl(const CloudItem&, const std::string&, const std::string&, std::string&,
                            std::string& err) override {
        err = "Google Drive: pre-authenticated URLs not supported; use ReadRange()";
        return false;
    }

    bool ListFolder(const std::string& root, const std::string& folder_id, const std::string& token,
                    const std::function<void(const CloudItem&)>& cb, std::string& cursor,
                    std::string& err) override;

    bool CreateUploadSession(const std::string& root, const std::string& parent_id,
                             const std::string& name, int64_t total_size, const std::string& token,
                             CloudUploadSession& out, std::string& err) override;

    bool UploadChunk(const CloudUploadSession& s, const char* data, int64_t off, int64_t size,
                     bool last, const std::string& token, std::string& err) override;

    bool DeleteItem(const std::string& root, const std::string& id, const std::string& token,
                    std::string& err) override;

    bool CreateFolder(const std::string& root, const std::string& parent_id,
                      const std::string& name, const std::string& token, CloudItem& out,
                      std::string& err) override;

    bool CopyItem(const std::string& root, const std::string& src_id,
                  const std::string& dst_parent_id, const std::string& dst_name,
                  const std::string& token, std::string& err) override;

  private:
    // Path resolution: Google Drive has no native path API.
    // We walk the tree: root → child named X → child named Y → …
    bool ResolvePathWalk(const std::string& start_folder_id,
                         const std::vector<std::string>& segments, const std::string& token,
                         CloudItem& out, std::string& err);

    // Build download URL for a given file ID
    std::string DownloadUrl(const std::string& file_id) const {
        return "https://www.googleapis.com/drive/v3/files/" + file_id +
               "?alt=media&supportsAllDrives=true";
    }

    CloudHttpClient& http_;
};

// ─────────────────────────────────────────────────────────────────────────────
// GDriveOAuthProvider
// OAuth2 PKCE flow for Google Drive (user-facing interactive auth).
// ─────────────────────────────────────────────────────────────────────────────
class GDriveOAuthProvider : public OAuth2AuthBase {
  public:
    GDriveOAuthProvider(std::string client_id, std::string client_secret)
        : OAuth2AuthBase("gdrive"), client_id_(std::move(client_id)),
          client_secret_(std::move(client_secret)) {}

  protected:
    bool AcquireToken(std::string& err) override; // Device Code Flow (Google)
    bool RefreshToken(std::string& err) override;

  private:
    std::string client_id_, client_secret_;
    static constexpr const char* kScope = "https://www.googleapis.com/auth/drive";
    static constexpr const char* kTokenUrl = "https://oauth2.googleapis.com/token";
    static constexpr const char* kDeviceUrl = "https://oauth2.googleapis.com/device/code";
};

// ─────────────────────────────────────────────────────────────────────────────
// GDriveServiceAccountAuth
// JWT-based auth for service accounts (Google ADC / key file).
// Best for server-side pipelines and CI/CD.
// ─────────────────────────────────────────────────────────────────────────────
class GDriveServiceAccountAuth : public ICloudAuthProvider {
  public:
    // key_json: contents of the service account JSON key file
    explicit GDriveServiceAccountAuth(std::string key_json);

    bool GetAccessToken(std::string& out, std::string& err) override;
    std::string ProviderName() const override { return "gdrive-sa"; }

  private:
    bool RefreshServiceAccountToken(std::string& err);
    std::string SignJWT() const;

    std::string project_id_, client_email_, private_key_id_, private_key_;
    OAuth2TokenBundle token_;
    std::mutex mu_;
};

} // namespace duckdb
