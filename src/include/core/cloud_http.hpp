#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// HttpRequest / HttpResponse
// Shared HTTP plumbing used by all backends.
// ─────────────────────────────────────────────────────────────────────────────
struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::unordered_map<std::string, std::string> headers;
    std::string body; // for POST/PUT with string body
    const char* body_data = nullptr;
    int64_t body_size = 0; // for PUT with binary body

    // Range request helpers
    void SetRange(int64_t offset, int64_t length) {
        headers["Range"] =
            "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + length - 1);
    }
    void SetBearerAuth(const std::string& token) {
        if (!token.empty())
            headers["Authorization"] = "Bearer " + token;
    }
    void SetContentRange(int64_t offset, int64_t length, int64_t total) {
        headers["Content-Range"] = "bytes " + std::to_string(offset) + "-" +
                                   std::to_string(offset + length - 1) + "/" +
                                   (total >= 0 ? std::to_string(total) : "*");
    }
};

struct HttpResponse {
    long status = 0;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    bool ok() const { return status >= 200 && status < 300; }
    std::string header(const std::string& k) const {
        auto it = headers.find(k);
        return it == headers.end() ? "" : it->second;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CloudHttpClient
// Shared libcurl wrapper with:
//   - automatic 429/503 retry with exponential backoff
//   - direct-to-buffer range reads (zero-copy for Parquet footer reads)
//   - connection reuse (CURLOPT_TCP_KEEPALIVE)
//   - follow redirects (for pre-auth URL hops)
// ─────────────────────────────────────────────────────────────────────────────
// Retry policy for CloudHttpClient — declared outside class to avoid GCC nested-struct issues
struct CloudRetryPolicy {
    int max_retries = 4;
    int initial_wait_ms = 500;
    double backoff_factor = 2.0;
    int max_wait_ms = 30000;
    std::vector<int> retryable = {429, 500, 502, 503, 504};
};

class CloudHttpClient {
  public:
    CloudHttpClient();
    ~CloudHttpClient();

    // General-purpose request (body returned in response.body)
    HttpResponse Execute(const HttpRequest& req,
                         const CloudRetryPolicy& policy = CloudRetryPolicy());

    // Optimized range read: writes directly into caller's buffer.
    // Returns bytes written, or -1 on error.
    // Falls back to full download if server returns 200 instead of 206.
    int64_t ReadRange(const std::string& url, const std::string& access_token, int64_t offset,
                      int64_t length, char* out_buffer, std::string& err,
                      const CloudRetryPolicy& policy = CloudRetryPolicy());

    // Convenience wrappers
    HttpResponse Get(const std::string& url, const std::string& token,
                     const std::string& range_header = "");
    HttpResponse Post(const std::string& url, const std::string& token, const std::string& body,
                      const std::string& ct = "application/json");
    HttpResponse Put(const std::string& url, const char* data, int64_t size,
                     const std::unordered_map<std::string, std::string>& extra_headers = {});
    HttpResponse Delete(const std::string& url, const std::string& token);
    HttpResponse Patch(const std::string& url, const std::string& token, const std::string& body,
                       const std::string& ct = "application/json");

  private:
    static size_t WriteCb(char* p, size_t s, size_t n, void* ud);
    static size_t WriteBufCb(char* p, size_t s, size_t n, void* ud);
    static size_t HeaderCb(char* p, size_t s, size_t n, void* ud);

    CURL* MakeCurl(HttpResponse& resp);

    bool ShouldRetry(long status, const CloudRetryPolicy& policy);
    void WaitForRetry(int attempt, const CloudRetryPolicy& policy);
};

// ─────────────────────────────────────────────────────────────────────────────
// UrlUtil
// Safe URL parameter encoding using libcurl's built-in percent-encoding.
// ─────────────────────────────────────────────────────────────────────────────
struct UrlUtil {
    // Percent-encode a single string (for query params, path segments)
    static std::string Encode(const std::string& s);
    // Build a query string from key-value pairs (returns "k1=v1&k2=v2")
    static std::string BuildQuery(const std::vector<std::pair<std::string, std::string>>& params);
};

// ─────────────────────────────────────────────────────────────────────────────
// JsonUtil
// Minimal JSON extraction — avoids a heavy dependency for simple API responses.
// For complex responses, providers can link nlohmann/json or simdjson.
// ─────────────────────────────────────────────────────────────────────────────
struct JsonUtil {
    static std::string GetString(const std::string& json, const std::string& key);
    static int64_t GetInt64(const std::string& json, const std::string& key);
    static bool GetBool(const std::string& json, const std::string& key);
    // Extract array of raw JSON objects from "key": [{...}, {...}]
    static std::vector<std::string> GetArray(const std::string& json, const std::string& key);
    // Safe JSON string builder with proper escaping
    static std::string MakeObject(const std::vector<std::pair<std::string, std::string>>& kv);
    // Escape a string for safe inclusion in JSON (handles quotes, backslashes, control chars)
    static std::string EscapeJsonString(const std::string& s);
};

} // namespace duckdb
