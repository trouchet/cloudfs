#include "providers/gdrive_backend.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace duckdb {

// ─── GDriveOAuthProvider ──────────────────────────────────────────────────────
bool GDriveOAuthProvider::AcquireToken(std::string& err) {
    // Google Device Authorization Grant (RFC 8628)
    std::string body = "client_id=" + client_id_ + "&scope=" + std::string(kScope);
    std::string resp = PostForm(kDeviceUrl, body, err);
    if (resp.empty())
        return false;

    std::string device_code = JsonGet(resp, "device_code");
    std::string user_code = JsonGet(resp, "user_code");
    std::string verify_url = JsonGet(resp, "verification_url");
    int expires_in = JsonGetInt(resp, "expires_in");
    int interval = std::max(JsonGetInt(resp, "interval"), 5);

    if (device_code.empty()) {
        err = "no device_code from Google";
        return false;
    }
    std::cerr << "\n[cloudfs Google Drive] Authenticate at: " << verify_url
              << "\nCode: " << user_code << "\nWaiting...\n";

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(expires_in);
    while (std::chrono::system_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        std::string b = "client_id=" + client_id_ + "&client_secret=" + client_secret_ +
                        "&device_code=" + device_code +
                        "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
        std::string r = PostForm(kTokenUrl, b, err);
        if (r.empty())
            continue;
        std::string ec = JsonGet(r, "error");
        if (ec == "authorization_pending" || ec == "slow_down") {
            if (ec == "slow_down")
                interval += 5;
            continue;
        }
        if (!ec.empty()) {
            err = ec;
            return false;
        }
        std::string at = JsonGet(r, "access_token");
        if (at.empty())
            continue;
        token_.SetFromResponse(at, JsonGet(r, "refresh_token"),
                               std::max(JsonGetInt(r, "expires_in"), 3600));
        std::cerr << "[cloudfs Google Drive] Authenticated.\n";
        return true;
    }
    err = "timed out";
    return false;
}

bool GDriveOAuthProvider::RefreshToken(std::string& err) {
    std::string body = "client_id=" + client_id_ + "&client_secret=" + client_secret_ +
                       "&refresh_token=" + token_.refresh_token + "&grant_type=refresh_token";
    std::string resp = PostForm(kTokenUrl, body, err);
    if (resp.empty() || !JsonGet(resp, "error").empty())
        return false;
    token_.SetFromResponse(JsonGet(resp, "access_token"), token_.refresh_token,
                           std::max(JsonGetInt(resp, "expires_in"), 3600));
    return !token_.access_token.empty();
}

// ─── GDriveServiceAccountAuth ─────────────────────────────────────────────────
GDriveServiceAccountAuth::GDriveServiceAccountAuth(std::string key_json) {
    // Parse JSON key file fields (inline helper — no OAuth2AuthBase dependency)
    auto get = [&](const std::string& k) -> std::string {
        auto search = "\"" + k + "\"";
        auto pos = key_json.find(search);
        if (pos == std::string::npos)
            return "";
        pos = key_json.find(':', pos + search.size());
        if (pos == std::string::npos)
            return "";
        while (++pos < key_json.size() && (key_json[pos] == ' ' || key_json[pos] == '\t'))
            ;
        if (pos >= key_json.size() || key_json[pos] != '"')
            return "";
        ++pos;
        std::string v;
        while (pos < key_json.size() && key_json[pos] != '"') {
            if (key_json[pos] == '\\' && pos + 1 < key_json.size()) {
                ++pos;
                v += key_json[pos];
            } else
                v += key_json[pos];
            ++pos;
        }
        return v;
    };
    project_id_ = get("project_id");
    client_email_ = get("client_email");
    private_key_id_ = get("private_key_id");
    private_key_ = get("private_key");
    // Unescape \n in private key
    for (size_t p = 0; (p = private_key_.find("\\n", p)) != std::string::npos; p += 1)
        private_key_.replace(p, 2, "\n");
}

bool GDriveServiceAccountAuth::GetAccessToken(std::string& out, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    if (token_.IsValid() && !token_.IsExpiringSoon()) {
        out = token_.access_token;
        return true;
    }
    if (!RefreshServiceAccountToken(err))
        return false;
    out = token_.access_token;
    return true;
}

bool GDriveServiceAccountAuth::RefreshServiceAccountToken(std::string& err) {
    // Build JWT assertion for service account
    // For brevity: this calls the Google token endpoint with a signed JWT.
    // Full implementation requires RSA-SHA256 signing (OpenSSL).
    // Header: {"alg":"RS256","typ":"JWT","kid":"<private_key_id>"}
    // Payload: {"iss":"<client_email>","scope":"...","aud":"...","exp":...,"iat":...}
    // Signature: RSA-SHA256(base64url(header)+"."+base64url(payload), private_key)
    //
    // Stub: subclasses or downstream code can provide the signed JWT directly.
    err = "GDriveServiceAccountAuth: JWT signing not yet implemented in this build. "
          "Use PROVIDER oauth or pass a pre-obtained token via PROVIDER token.";
    return false;
    // TODO: integrate OpenSSL RSA signing via EVP_DigestSign*
}

} // namespace duckdb
