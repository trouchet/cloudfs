#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// ICloudAuthProvider
//
// Abstract authentication contract.  Each provider supplies its own
// implementation (OAuth2 PKCE, Device Code, API key, service account, etc.)
// The CloudFileSystem only calls GetAccessToken(); everything else is
// implementation detail of the concrete auth class.
// ─────────────────────────────────────────────────────────────────────────────
class ICloudAuthProvider {
public:
    virtual ~ICloudAuthProvider() = default;

    // Returns a valid bearer token (refreshing transparently if needed).
    // Returns false and populates `err` on unrecoverable failure.
    virtual bool GetAccessToken(std::string &out_token, std::string &err) = 0;

    // Optional: force re-authentication (e.g. after 401 Unauthorized).
    virtual bool Reauthenticate(std::string &err) { return GetAccessToken(*new std::string, err); }

    // Provider name for error messages ("sharepoint", "gdrive", …).
    virtual std::string ProviderName() const = 0;

    // Called by the Secret Manager to persist refresh tokens across sessions.
    virtual void SerializeState(std::unordered_map<std::string,std::string> &out) const {}
    virtual void DeserializeState(const std::unordered_map<std::string,std::string> &in) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// StaticTokenAuth
// Simplest auth: a fixed access token with no refresh.
// Used for CI/CD, service principals, and testing.
// ─────────────────────────────────────────────────────────────────────────────
class StaticTokenAuth : public ICloudAuthProvider {
public:
    explicit StaticTokenAuth(std::string provider_name, std::string token)
        : provider_(std::move(provider_name)), token_(std::move(token)) {}

    bool GetAccessToken(std::string &out, std::string &err) override {
        if (token_.empty()) { err = provider_ + ": token is empty"; return false; }
        out = token_;
        return true;
    }
    std::string ProviderName() const override { return provider_; }

private:
    std::string provider_;
    std::string token_;
};

// ─────────────────────────────────────────────────────────────────────────────
// OAuth2TokenBundle
// Shared token state usable by any OAuth2 flow (Device Code, PKCE, etc.)
// ─────────────────────────────────────────────────────────────────────────────
struct OAuth2TokenBundle {
    std::string access_token;
    std::string refresh_token;
    std::chrono::system_clock::time_point expires_at;

    bool IsValid() const {
        return !access_token.empty() &&
               std::chrono::system_clock::now() < expires_at;
    }
    bool IsExpiringSoon() const {
        return IsValid() &&
               (expires_at - std::chrono::system_clock::now()) < std::chrono::minutes(5);
    }
    void SetFromResponse(const std::string &access, const std::string &refresh, int expires_in) {
        access_token  = access;
        refresh_token = refresh;
        expires_at    = std::chrono::system_clock::now() + std::chrono::seconds(expires_in);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// OAuth2AuthBase
// Reusable base for any OAuth2-based auth provider.
// Concrete classes implement AcquireToken() (Device Code, PKCE, client_credentials…)
// and RefreshToken().
// ─────────────────────────────────────────────────────────────────────────────
class OAuth2AuthBase : public ICloudAuthProvider {
public:
    explicit OAuth2AuthBase(std::string provider_name)
        : provider_(std::move(provider_name)) {}

    bool GetAccessToken(std::string &out, std::string &err) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (token_.IsValid() && !token_.IsExpiringSoon()) {
            out = token_.access_token;
            return true;
        }
        if (!token_.refresh_token.empty() && !token_.IsValid()) {
            if (RefreshToken(err)) { out = token_.access_token; return true; }
            // Fall through to full re-auth
        }
        if (!AcquireToken(err)) return false;
        out = token_.access_token;
        return true;
    }

    std::string ProviderName() const override { return provider_; }

    void SerializeState(std::unordered_map<std::string,std::string> &out) const override {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
        out["access_token"]  = token_.access_token;
        out["refresh_token"] = token_.refresh_token;
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            token_.expires_at.time_since_epoch()).count();
        out["expires_at"] = std::to_string(epoch);
    }
    void DeserializeState(const std::unordered_map<std::string,std::string> &in) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto get = [&](const std::string &k) -> std::string {
            auto it = in.find(k); return it == in.end() ? "" : it->second;
        };
        token_.access_token  = get("access_token");
        token_.refresh_token = get("refresh_token");
        auto ts = get("expires_at");
        if (!ts.empty()) {
            token_.expires_at = std::chrono::system_clock::from_time_t(
                static_cast<time_t>(strtoll(ts.c_str(), nullptr, 10)));
        }
    }

protected:
    // Subclass implements these two:
    virtual bool AcquireToken(std::string &err)  = 0;
    virtual bool RefreshToken(std::string &err)  = 0;

    OAuth2TokenBundle token_;
    mutable std::mutex mu_;
    std::string provider_;

    // Helper: POST application/x-www-form-urlencoded and return body
    static std::string PostForm(const std::string &url,
                                 const std::string &body,
                                 std::string &err);
    static std::string JsonGet(const std::string &json, const std::string &key);
    static int         JsonGetInt(const std::string &json, const std::string &key);
};

} // namespace duckdb
