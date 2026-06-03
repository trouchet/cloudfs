#pragma once
#include "../core/cloud_backend.hpp"
#include "../core/cloud_http.hpp"

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// OneDriveBackend
// Scheme: odfs://
// Auth:   OAuth2 Device Code (same Microsoft identity as SharePoint)
//         or static token
// API:    Microsoft Graph v1.0  /me/drive  or  /users/{id}/drive
//
// URL format:
//   odfs://me/Documents/report.parquet            (personal OneDrive)
//   odfs://user@tenant.com/Documents/data/*.csv   (specific user's OneDrive)
//
// Key difference from SharePoint:
//   - Root is a personal drive, not a SharePoint site + library
//   - No "Shared Documents" library concept; path is relative to drive root
//   - Same Graph API, different root URL pattern
// ─────────────────────────────────────────────────────────────────────────────
class OneDriveBackend : public ICloudBackend {
  public:
    explicit OneDriveBackend(CloudHttpClient& http) : http_(http) {}

    std::string Scheme() const override { return "odfs"; }
    std::string Name() const override { return "OneDrive"; }

    ProviderCapabilities Capabilities() const override {
        return {.supports_range_reads = true,
                .supports_resumable_uploads = true,
                .supports_server_side_copy = true,
                .supports_recursive_list = false,
                .needs_total_size_upfront = false,
                .upload_chunk_alignment = 320 * 1024, // same Graph API rules
                .min_upload_chunk = 320 * 1024,
                .max_upload_chunk = 60 * 1024 * 1024};
    }

    bool ParseUrl(const std::string& url, std::string& out_root, std::string& out_path,
                  std::string& err) const override;

    bool ResolveRoot(const std::string& root, const std::string& token, std::string& out_id,
                     std::string& err) override;

    bool Stat(const std::string& root, const std::string& path, const std::string& token,
              CloudItem& out, std::string& err) override;

    int64_t ReadRange(const CloudItem& item, const std::string& root, const std::string& token,
                      int64_t off, int64_t len, char* buf, std::string& err) override;

    bool RefreshDownloadUrl(const CloudItem& item, const std::string& root,
                            const std::string& token, std::string& out_url,
                            std::string& err) override;

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

  private:
    // Builds the Graph API base URL for a given root
    // root="me"          → https://graph.microsoft.com/v1.0/me/drive
    // root="user@t.com"  → https://graph.microsoft.com/v1.0/users/user@t.com/drive
    std::string DriveBaseUrl(const std::string& root_id) const;

    CloudHttpClient& http_;
};

// ─────────────────────────────────────────────────────────────────────────────
// OneDriveAuth (same Microsoft Device Code Flow as SharePoint)
// Re-uses SharePointDeviceCodeAuth with different default scope.
// ─────────────────────────────────────────────────────────────────────────────
class OneDriveAuth : public OAuth2AuthBase {
  public:
    OneDriveAuth(std::string tenant_id, std::string client_id)
        : OAuth2AuthBase("onedrive"), tenant_id_(std::move(tenant_id)),
          client_id_(std::move(client_id)) {}

  protected:
    bool AcquireToken(std::string& err) override;
    bool RefreshToken(std::string& err) override;

  private:
    std::string tenant_id_, client_id_;
    // Scope: Files.ReadWrite + offline_access
    static constexpr const char* kScope =
        "https://graph.microsoft.com/Files.ReadWrite offline_access";
};

} // namespace duckdb
