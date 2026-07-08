#include "providers/gdrive_backend.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

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
GDriveServiceAccountAuth::GDriveServiceAccountAuth(std::string key_json)
    : OAuth2AuthBase("gdrive-sa") {
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

// Base64url encoding helper (RFC 4648 §5, no padding)
static std::string GDriveBase64Url(const unsigned char* data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = static_cast<uint8_t>(data[i]) << 16;
        if (i + 1 < len) val |= static_cast<uint8_t>(data[i + 1]) << 8;
        if (i + 2 < len) val |= static_cast<uint8_t>(data[i + 2]);
        out += tbl[(val >> 18) & 63];
        out += tbl[(val >> 12) & 63];
        out += (i + 1 < len) ? tbl[(val >> 6) & 63] : '=';
        out += (i + 2 < len) ? tbl[val & 63] : '=';
    }
    for (auto& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

static std::string GDriveBase64UrlStr(const std::string& s) {
    return GDriveBase64Url(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

std::string GDriveServiceAccountAuth::SignJWT() const {
    using namespace std::chrono;
    auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();

    std::string header =
        "{\"alg\":\"RS256\",\"typ\":\"JWT\",\"kid\":\"" + private_key_id_ + "\"}";
    std::string payload =
        "{\"iss\":\"" + client_email_ + "\""
        ",\"scope\":\"https://www.googleapis.com/auth/drive\""
        ",\"aud\":\"https://oauth2.googleapis.com/token\""
        ",\"iat\":" + std::to_string(now) +
        ",\"exp\":" + std::to_string(now + 3600) + "}";

    std::string signing_input = GDriveBase64UrlStr(header) + "." + GDriveBase64UrlStr(payload);

    BIO* bio = BIO_new_mem_buf(private_key_.c_str(), static_cast<int>(private_key_.size()));
    if (!bio) return "";
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return "";

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return "";
    }

    std::string jwt;
    bool ok = EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
              EVP_DigestSignUpdate(mdctx, signing_input.c_str(), signing_input.size()) == 1;
    if (ok) {
        size_t sig_len = 0;
        ok = EVP_DigestSignFinal(mdctx, nullptr, &sig_len) == 1 && sig_len > 0;
        if (ok) {
            std::vector<unsigned char> sig(sig_len);
            ok = EVP_DigestSignFinal(mdctx, sig.data(), &sig_len) == 1;
            if (ok)
                jwt = signing_input + "." + GDriveBase64Url(sig.data(), sig_len);
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return jwt;
}

bool GDriveServiceAccountAuth::AcquireToken(std::string& err) {
    std::string jwt = SignJWT();
    if (jwt.empty()) {
        err = "gdrive-sa: JWT signing failed — verify private_key in service account JSON";
        return false;
    }
    // grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer (URL-encoded)
    std::string body =
        "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
        "&assertion=" + jwt;
    std::string resp = PostForm("https://oauth2.googleapis.com/token", body, err);
    if (resp.empty()) return false;
    std::string ec = JsonGet(resp, "error");
    if (!ec.empty()) {
        err = "gdrive-sa: " + ec + " — " + JsonGet(resp, "error_description");
        return false;
    }
    std::string at = JsonGet(resp, "access_token");
    if (at.empty()) {
        err = "gdrive-sa: no access_token in response";
        return false;
    }
    token_.SetFromResponse(at, "", std::max(JsonGetInt(resp, "expires_in"), 3600));
    std::cerr << "[cloudfs Google Drive] Service account authenticated.\n";
    return true;
}

} // namespace duckdb
