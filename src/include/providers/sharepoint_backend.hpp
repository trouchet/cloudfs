#pragma once
#include "../core/cloud_auth.hpp"
#include "../core/cloud_backend.hpp"
#include "../core/cloud_http.hpp"

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// SharePointBackend
// Scheme: spfs://
// Auth:   OAuth2 Device Code Flow (tenant/client configurable)
//         or static Bearer token
// API:    Microsoft Graph v1.0  /drives/{id}/items/{id}
//
// URL format:
//   spfs://tenant.sharepoint.com/sites/SiteName/LibraryOrPath/file.parquet
//   spfs://tenant.sharepoint.com/teams/TeamName/Documents/**/*.parquet
// ─────────────────────────────────────────────────────────────────────────────
class SharePointBackend : public ICloudBackend {
  public:
    explicit SharePointBackend(CloudHttpClient& http) : http_(http) {}

    std::string Scheme() const override { return "spfs"; }
    std::string Name() const override { return "SharePoint"; }

    ProviderCapabilities Capabilities() const override {
        return {
            .supports_range_reads = true,
            .supports_resumable_uploads = true,
            .supports_server_side_copy = true,
            .supports_recursive_list = false,
            .needs_total_size_upfront = false,
            .upload_chunk_alignment = 320 * 1024, // 320 KiB required
            .min_upload_chunk = 320 * 1024,
            .max_upload_chunk = 60 * 1024 * 1024 // 60 MiB
        };
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

    bool CopyItem(const std::string& root, const std::string& src_id,
                  const std::string& dst_parent_id, const std::string& dst_name,
                  const std::string& token, std::string& err) override;

  private:
    // root = "https://tenant.sharepoint.com|/sites/SiteName" → drive_id
    struct SiteInfo {
        std::string site_id;
        std::string drive_id;
    };
    bool GetSiteInfo(const std::string& base_url, const std::string& site_path,
                     const std::string& token, SiteInfo& out, std::string& err);

    CloudHttpClient& http_;
};

// ─────────────────────────────────────────────────────────────────────────────
// SharePointDeviceCodeAuth
// Microsoft Device Code Flow for interactive login.
// ─────────────────────────────────────────────────────────────────────────────
class SharePointDeviceCodeAuth : public OAuth2AuthBase {
  public:
    SharePointDeviceCodeAuth(std::string tenant_id, std::string client_id, std::string scope)
        : OAuth2AuthBase("sharepoint"), tenant_id_(std::move(tenant_id)),
          client_id_(std::move(client_id)), scope_(std::move(scope)) {}

  protected:
    bool AcquireToken(std::string& err) override;
    bool RefreshToken(std::string& err) override;

  private:
    bool PollDeviceCode(const std::string& device_code, int interval, int expires_in,
                        std::string& err);
    std::string tenant_id_, client_id_, scope_;
};

} // namespace duckdb
