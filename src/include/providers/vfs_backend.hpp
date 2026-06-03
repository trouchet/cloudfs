#pragma once
#include "../core/cloud_backend.hpp"
#include "../core/cloud_http.hpp"

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// VFSBackend  ("Virtual File Server")
// Scheme: vfs://
//
// Connects to a `cloudfs-agent` process running on any server.
// The agent is a ~4 MB static binary (Go) that wraps the local filesystem
// with an authenticated HTTP API supporting range reads.
//
// URL format:
//   vfs://hostname:8765/absolute/path/to/file.parquet
//   vfs://ec2-54-123-0-1.compute.amazonaws.com:8765/data/**/*.parquet
//   vfs+tls://hostname:8766/data/file.parquet    (TLS variant)
//
// Agent API (all endpoints require Bearer token):
//   GET  /v1/stat?path=...              → JSON CloudItem
//   GET  /v1/read?path=...              → file bytes (Range header supported)
//   GET  /v1/list?path=...&cursor=...   → JSON {items:[...], next_cursor:""}
//   POST /v1/mkdir    body:{path}       → 200 OK
//   DELETE /v1/delete?path=...          → 204 No Content
//   POST /v1/upload/start  body:{path}  → {session_id}
//   PUT  /v1/upload/chunk?session=...   → 202 Accepted
//   POST /v1/upload/finish?session=...  → 201 Created
//
// The agent token is passed as the "access token" via CREATE SECRET.
//
// Key advantages over SFTP:
//   ✓ No SSH key management — one bearer token per agent
//   ✓ Native HTTP range reads — DuckDB's parallel prefetch works fully
//   ✓ Multiplexed connections (HTTP/1.1 keep-alive or HTTP/2)
//   ✓ TLS with self-signed certs for private networks
//   ✓ Works through firewalls (port 443/8765 vs 22)
//   ✓ Rate limiting and access control in the agent
//   ✓ Same agent binary works on EC2, DigitalOcean, Hetzner, bare metal
// ─────────────────────────────────────────────────────────────────────────────

class VFSBackend : public ICloudBackend {
public:
    explicit VFSBackend(CloudHttpClient &http) : http_(http) {}

    std::string Scheme()      const override { return "vfs"; }
    std::string Name()        const override { return "VFS Agent"; }

    ProviderCapabilities Capabilities() const override {
        return {
            .supports_range_reads       = true,
            .supports_resumable_uploads = true,
            .supports_server_side_copy  = false,
            .supports_recursive_list    = false,
            .needs_total_size_upfront   = false,
            .upload_chunk_alignment     = 1,            // agent accepts any chunk size
            .min_upload_chunk           = 1,
            .max_upload_chunk           = 128 * 1024 * 1024
        };
    }

    // vfs://host:port/path  →  root="http://host:port"  path="/path"
    // vfs+tls://host:port/path → root="https://host:port"  path="/path"
    bool ParseUrl(const std::string &url,
                  std::string &out_root, std::string &out_path,
                  std::string &err) const override;

    bool ResolveRoot(const std::string &root, const std::string &,
                     std::string &out_id, std::string &err) override {
        // Ping the agent to verify connectivity
        out_id = root; return true;
    }

    bool    Stat(const std::string &root, const std::string &path,
                 const std::string &token, CloudItem &out, std::string &err) override;

    int64_t ReadRange(const CloudItem &item, const std::string &root,
                      const std::string &token,
                      int64_t off, int64_t len, char *buf, std::string &err) override;

    bool    ListFolder(const std::string &root, const std::string &folder_id,
                       const std::string &token,
                       const std::function<void(const CloudItem &)> &cb,
                       std::string &cursor, std::string &err) override;

    bool    CreateUploadSession(const std::string &root, const std::string &parent_id,
                                const std::string &name, int64_t total_size,
                                const std::string &token,
                                CloudUploadSession &out, std::string &err) override;

    bool    UploadChunk(const CloudUploadSession &s, const char *data,
                        int64_t off, int64_t size, bool last,
                        const std::string &token, std::string &err) override;

    bool    DeleteItem(const std::string &root, const std::string &id,
                       const std::string &token, std::string &err) override;

    bool    CreateFolder(const std::string &root, const std::string &parent_id,
                         const std::string &name, const std::string &token,
                         CloudItem &out, std::string &err) override;

    // Check agent health — returns version string or error
    bool Ping(const std::string &root, const std::string &token,
              std::string &out_version, std::string &err);

private:
    CloudItem ParseItem(const std::string &json) const;
    CloudHttpClient &http_;
};

// ─────────────────────────────────────────────────────────────────────────────
// VFSAuth — just a bearer token (set by the agent admin)
// ─────────────────────────────────────────────────────────────────────────────
class VFSAuth : public ICloudAuthProvider {
public:
    explicit VFSAuth(std::string token) : token_(std::move(token)) {}

    bool GetAccessToken(std::string &out, std::string &err) override {
        if (token_.empty()) { err = "vfs: token is empty"; return false; }
        out = token_; return true;
    }
    std::string ProviderName() const override { return "vfs"; }

private:
    std::string token_;
};

} // namespace duckdb
