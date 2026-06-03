#include "core/cloud_auth.hpp"
#include <curl/curl.h>
#include <cstring>

namespace duckdb {

// ─── Shared HTTP POST helper ──────────────────────────────────────────────────
static size_t WriteCb(char *p, size_t s, size_t n, void *ud) {
    static_cast<std::string*>(ud)->append(p, s*n); return s*n;
}

std::string OAuth2AuthBase::PostForm(const std::string &url,
                                      const std::string &body,
                                      std::string &err) {
    std::string resp;
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL,          url.c_str());
    curl_easy_setopt(c, CURLOPT_POST,         1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,   body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE,(long)body.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,WriteCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,    &resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,      30L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER,1L);
    struct curl_slist *h = curl_slist_append(nullptr,
        "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(h); curl_easy_cleanup(c);
    if (rc != CURLE_OK) { err = curl_easy_strerror(rc); return ""; }
    return resp;
}

std::string OAuth2AuthBase::JsonGet(const std::string &json, const std::string &key) {
    auto s = "\"" + key + "\"";
    auto p = json.find(s);
    if (p == std::string::npos) return "";
    p = json.find(':', p + s.size());
    if (p == std::string::npos) return "";
    while (++p < json.size() && (json[p]==' '||json[p]=='\t'));
    if (p >= json.size()) return "";
    if (json[p] != '"') {
        std::string v;
        while (p < json.size() && json[p]!=',' && json[p]!='}') v += json[p++];
        while (!v.empty() && (v.back()==' '||v.back()=='\r'||v.back()=='\n')) v.pop_back();
        return v;
    }
    ++p; std::string v;
    while (p < json.size() && json[p] != '"') v += json[p++];
    return v;
}

int OAuth2AuthBase::JsonGetInt(const std::string &json, const std::string &key) {
    auto v = JsonGet(json, key);
    return v.empty() ? 0 : (int)strtol(v.c_str(), nullptr, 10);
}

} // namespace duckdb
