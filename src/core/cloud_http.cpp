#include "core/cloud_http.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>

#include <curl/curl.h>

namespace duckdb {

// ─── Write callbacks ──────────────────────────────────────────────────────────
size_t CloudHttpClient::WriteCb(char* p, size_t s, size_t n, void* ud) {
    static_cast<HttpResponse*>(ud)->body.append(p, s * n);
    return s * n;
}

struct BufCtx {
    char* buf;
    int64_t cap;
    int64_t* written;
};
size_t CloudHttpClient::WriteBufCb(char* p, size_t s, size_t n, void* ud) {
    auto* c = static_cast<BufCtx*>(ud);
    int64_t bytes = (int64_t)(s * n);
    int64_t space = c->cap - *c->written;
    int64_t copy = std::min(bytes, space);
    if (copy > 0) {
        memcpy(c->buf + *c->written, p, copy);
        *c->written += copy;
    }
    return s * n; // always consume; excess is discarded when server ignores Range
}

size_t CloudHttpClient::HeaderCb(char* p, size_t s, size_t n, void* ud) {
    auto* r = static_cast<HttpResponse*>(ud);
    std::string h(p, s * n);
    auto colon = h.find(':');
    if (colon != std::string::npos) {
        std::string key = h.substr(0, colon);
        std::string val = h.substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
            val.erase(val.begin());
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' '))
            val.pop_back();
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        r->headers[key] = val;
    }
    return s * n;
}

// ─── CURL setup ───────────────────────────────────────────────────────────────
CURL* CloudHttpClient::MakeCurl(HttpResponse& resp) {
    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, HeaderCb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &resp);
    
    // Security: verify both peer certificate AND hostname
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);  // Verify hostname matches cert
    
    // Timeout strategy: separate connection and total timeouts
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 30000L);  // 30s connection timeout
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);              // 120s total timeout
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);               // Thread-safe, no signals
    
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    return c;
}

static struct curl_slist* BuildHeaders(const std::unordered_map<std::string, std::string>& hdrs) {
    struct curl_slist* list = nullptr;
    for (auto& [k, v] : hdrs)
        list = curl_slist_append(list, (k + ": " + v).c_str());
    return list;
}

CloudHttpClient::CloudHttpClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}
CloudHttpClient::~CloudHttpClient() {}

bool CloudHttpClient::ShouldRetry(long status, const CloudRetryPolicy& p) {
    return std::find(p.retryable.begin(), p.retryable.end(), (int)status) != p.retryable.end();
}

void CloudHttpClient::WaitForRetry(int attempt, const CloudRetryPolicy& p) {
    int ms = (int)(p.initial_wait_ms * std::pow(p.backoff_factor, attempt));
    ms = std::min(ms, p.max_wait_ms);
    // Add jitter: randomize ±20% to avoid thundering herd
    int jitter = (ms * 20) / 100;
    int jitter_offset = (rand() % (jitter * 2 + 1)) - jitter;
    ms = std::max(0, ms + jitter_offset);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ─── Execute ──────────────────────────────────────────────────────────────────
HttpResponse CloudHttpClient::Execute(const HttpRequest& req, const CloudRetryPolicy& policy) {
    for (int attempt = 0; attempt <= policy.max_retries; ++attempt) {
        HttpResponse resp;
        CURL* c = MakeCurl(resp);
        curl_easy_setopt(c, CURLOPT_URL, req.url.c_str());

        if (req.method == "POST") {
            curl_easy_setopt(c, CURLOPT_POST, 1L);
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        } else if (req.method == "PUT") {
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
            if (req.body_data) {
                curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body_data);
                curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req.body_size);
            } else {
                curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str());
                curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
            }
        } else if (req.method == "DELETE") {
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (req.method == "PATCH") {
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        }

        auto* hdrs = BuildHeaders(req.headers);
        if (hdrs)
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

        CURLcode rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &resp.status);
        if (hdrs)
            curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);

        if (rc != CURLE_OK || !ShouldRetry(resp.status, policy))
            return resp;
        if (attempt < policy.max_retries)
            WaitForRetry(attempt, policy);
    }
    return {};
}

// ─── ReadRange ────────────────────────────────────────────────────────────────
int64_t CloudHttpClient::ReadRange(const std::string& url, const std::string& access_token,
                                   int64_t offset, int64_t length, char* out_buffer,
                                   std::string& err, const CloudRetryPolicy& policy) {
    for (int attempt = 0; attempt <= policy.max_retries; ++attempt) {
        int64_t written = 0;
        BufCtx ctx{out_buffer, length, &written};

        HttpResponse resp_hdr; // for headers only
        CURL* c = curl_easy_init();
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        
        // Security: verify both peer certificate AND hostname
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // Timeout strategy: separate connection and total timeouts
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteBufCb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, HeaderCb);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &resp_hdr);

        char range_hdr[64];
        snprintf(range_hdr, sizeof(range_hdr), "Range: bytes=%lld-%lld", (long long)offset,
                 (long long)(offset + length - 1));

        struct curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, range_hdr);
        if (!access_token.empty())
            hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + access_token).c_str());
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

        CURLcode rc = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);

        if (rc != CURLE_OK) {
            err = std::string("curl: ") + curl_easy_strerror(rc);
            if (attempt < policy.max_retries) {
                WaitForRetry(attempt, policy);
                continue;
            }
            return -1;
        }
        if (ShouldRetry(status, policy) && attempt < policy.max_retries) {
            WaitForRetry(attempt, policy);
            continue;
        }
        if (status == 206)
            return written; // ideal path
        if (status == 200)
            return std::min(written, length); // server ignored Range
        err = "ReadRange HTTP " + std::to_string(status);
        return -1;
    }
    return -1;
}

// ─── Convenience wrappers ─────────────────────────────────────────────────────
HttpResponse CloudHttpClient::Get(const std::string& url, const std::string& tok,
                                  const std::string& range_hdr) {
    HttpRequest r;
    r.url = url;
    r.SetBearerAuth(tok);
    if (!range_hdr.empty())
        r.headers["Range"] = range_hdr;
    return Execute(r);
}
HttpResponse CloudHttpClient::Post(const std::string& url, const std::string& tok,
                                   const std::string& body, const std::string& ct) {
    HttpRequest r;
    r.method = "POST";
    r.url = url;
    r.body = body;
    r.SetBearerAuth(tok);
    r.headers["Content-Type"] = ct;
    return Execute(r);
}
HttpResponse CloudHttpClient::Put(const std::string& url, const char* data, int64_t sz,
                                  const std::unordered_map<std::string, std::string>& extra) {
    HttpRequest r;
    r.method = "PUT";
    r.url = url;
    r.body_data = data;
    r.body_size = sz;
    for (auto& [k, v] : extra)
        r.headers[k] = v;
    char cl[32];
    snprintf(cl, sizeof(cl), "%lld", (long long)sz);
    r.headers["Content-Length"] = cl;
    return Execute(r);
}
HttpResponse CloudHttpClient::Delete(const std::string& url, const std::string& tok) {
    HttpRequest r;
    r.method = "DELETE";
    r.url = url;
    r.SetBearerAuth(tok);
    return Execute(r);
}
HttpResponse CloudHttpClient::Patch(const std::string& url, const std::string& tok,
                                    const std::string& body, const std::string& ct) {
    HttpRequest r;
    r.method = "PATCH";
    r.url = url;
    r.body = body;
    r.SetBearerAuth(tok);
    r.headers["Content-Type"] = ct;
    return Execute(r);
}

// ─── URL encoding ─────────────────────────────────────────────────────────────
std::string UrlUtil::Encode(const std::string& s) {
    CURL* c = curl_easy_init();
    char* enc = curl_easy_escape(c, s.c_str(), (int)s.size());
    std::string result(enc);
    curl_free(enc);
    curl_easy_cleanup(c);
    return result;
}

std::string UrlUtil::BuildQuery(const std::vector<std::pair<std::string, std::string>>& params) {
    if (params.empty()) return "";
    std::string q;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) q += "&";
        q += Encode(params[i].first) + "=" + Encode(params[i].second);
    }
    return q;
}

// ─── JsonUtil ─────────────────────────────────────────────────────────────────
std::string JsonUtil::GetString(const std::string& json, const std::string& key) {
    auto search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return "";
    while (++pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        ;
    if (pos >= json.size() || json[pos] != '"')
        return "";
    ++pos;
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
            case '"':
                val += '"';
                break;
            case '\\':
                val += '\\';
                break;
            case 'n':
                val += '\n';
                break;
            case 'r':
                val += '\r';
                break;
            case 't':
                val += '\t';
                break;
            default:
                val += json[pos];
                break;
            }
        } else
            val += json[pos];
        ++pos;
    }
    return val;
}

int64_t JsonUtil::GetInt64(const std::string& json, const std::string& key) {
    auto search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return 0;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return 0;
    while (++pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        ;
    return (int64_t)strtoll(json.c_str() + pos, nullptr, 10);
}

bool JsonUtil::GetBool(const std::string& json, const std::string& key) {
    auto search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return false;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return false;
    while (++pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        ;
    return json.substr(pos, 4) == "true";
}

std::vector<std::string> JsonUtil::GetArray(const std::string& json, const std::string& key) {
    std::vector<std::string> items;
    auto search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return items;
    pos = json.find('[', pos);
    if (pos == std::string::npos)
        return items;
    int depth = 0;
    std::string cur;
    for (size_t i = pos + 1; i < json.size(); ++i) { // start AFTER the [
        char ch = json[i];
        if (ch == '{') {
            if (depth == 0)
                cur.clear(); // starting a new top-level item
            cur += ch;
            ++depth;
        } else if (ch == '}') {
            cur += ch;
            --depth;
            if (depth == 0)
                items.push_back(cur); // finished a top-level item
        } else if (ch == ']' && depth == 0) {
            break;
        } else if (depth > 0) {
            cur += ch;
        }
    }
    return items;
}

std::string JsonUtil::MakeObject(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string s = "{";
    for (size_t i = 0; i < kv.size(); ++i) {
        if (i)
            s += ",";
        s += "\"" + EscapeJsonString(kv[i].first) + "\":\"" + EscapeJsonString(kv[i].second) + "\"";
    }
    return s + "}";
}

std::string JsonUtil::EscapeJsonString(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b";  break;
        case '\f': result += "\\f";  break;
        case '\n': result += "\\n";  break;
        case '\r': result += "\\r";  break;
        case '\t': result += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", (int)c);
                result += buf;
            } else {
                result += c;
            }
        }
    }
    return result;
}

} // namespace duckdb
