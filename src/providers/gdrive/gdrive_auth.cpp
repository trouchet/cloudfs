#include "core/cloud_http.hpp"
#include "core/cloud_loopback.hpp"

#include "providers/gdrive_backend.hpp"

#include <chrono>
#include <iostream>
#include <utility>
#include <vector>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

namespace duckdb {

// Base64url encoding helper (RFC 4648 §5, no padding)
static std::string GDriveBase64Url(const unsigned char* data, size_t len) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = static_cast<uint8_t>(data[i]) << 16;
        if (i + 1 < len)
            val |= static_cast<uint8_t>(data[i + 1]) << 8;
        if (i + 2 < len)
            val |= static_cast<uint8_t>(data[i + 2]);
        out += tbl[(val >> 18) & 63];
        out += tbl[(val >> 12) & 63];
        out += (i + 1 < len) ? tbl[(val >> 6) & 63] : '=';
        out += (i + 2 < len) ? tbl[val & 63] : '=';
    }
    for (auto& c : out) {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    while (!out.empty() && out.back() == '=')
        out.pop_back();
    return out;
}

static std::string GDriveBase64UrlStr(const std::string& s) {
    return GDriveBase64Url(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

// ─── GDriveOAuthProvider ──────────────────────────────────────────────────────
std::string GDriveOAuthProvider::RandomUrlToken(size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1)
        return "";
    return GDriveBase64Url(buf.data(), buf.size());
}

std::string GDriveOAuthProvider::PkceChallenge(const std::string& code_verifier) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(code_verifier.data(), code_verifier.size(), digest, &len, EVP_sha256(),
                   nullptr) != 1)
        return "";
    return GDriveBase64Url(digest, len);
}

std::string GDriveOAuthProvider::BuildAuthUrl(const std::string& client_id,
                                              const std::string& redirect_uri,
                                              const std::string& state,
                                              const std::string& code_challenge) {
    return std::string(kAuthUrl) + "?" +
           UrlUtil::BuildQuery({{"client_id", client_id},
                                {"redirect_uri", redirect_uri},
                                {"response_type", "code"},
                                {"scope", kScope},
                                {"code_challenge", code_challenge},
                                {"code_challenge_method", "S256"},
                                {"state", state}});
}

bool GDriveOAuthProvider::AcquireToken(std::string& err) {
    // OAuth 2.0 for desktop apps: loopback redirect (RFC 8252) with PKCE (RFC 7636).
    if (client_id_.empty()) {
        err = "gdrive oauth: CLIENT_ID is required (an OAuth client of type \"Desktop app\")";
        return false;
    }
    // 32 random bytes encode to 43 characters, the minimum verifier length.
    std::string verifier = RandomUrlToken(32);
    std::string state = RandomUrlToken(16);
    std::string challenge = PkceChallenge(verifier);
    if (verifier.empty() || state.empty() || challenge.empty()) {
        err = "gdrive oauth: could not generate the PKCE parameters";
        return false;
    }

    LoopbackCodeReceiver receiver;
    if (!receiver.Listen(redirect_port_, err)) {
        err = "gdrive oauth: " + err;
        return false;
    }
    std::string redirect_uri = receiver.RedirectUri();

    std::cerr
        << "\n[cloudfs Google Drive] Open this URL in a browser on this machine to authorize:\n\n  "
        << BuildAuthUrl(client_id_, redirect_uri, state, challenge)
        << "\n\nWaiting for the redirect on " << redirect_uri << " (up to "
        << kAuthorizeTimeoutSeconds / 60 << " minutes)...\n";

    std::string code;
    if (!receiver.WaitForCode(state, kAuthorizeTimeoutSeconds, code, err)) {
        err = "gdrive oauth: " + err;
        return false;
    }
    receiver.Close();

    std::vector<std::pair<std::string, std::string>> form = {{"client_id", client_id_},
                                                             {"code", code},
                                                             {"code_verifier", verifier},
                                                             {"grant_type", "authorization_code"},
                                                             {"redirect_uri", redirect_uri}};
    if (!client_secret_.empty())
        form.emplace_back("client_secret", client_secret_);
    std::string resp = PostForm(kTokenUrl, UrlUtil::BuildQuery(form), err);
    if (resp.empty()) {
        err = "gdrive oauth: token request failed: " + err;
        return false;
    }
    std::string ec = JsonGet(resp, "error");
    if (!ec.empty()) {
        err = "gdrive oauth: " + ec + " — " + JsonGet(resp, "error_description");
        return false;
    }
    std::string at = JsonGet(resp, "access_token");
    if (at.empty()) {
        err = "gdrive oauth: no access_token in the token response";
        return false;
    }
    token_.SetFromResponse(at, JsonGet(resp, "refresh_token"),
                           std::max(JsonGetInt(resp, "expires_in"), 60));
    std::cerr << "[cloudfs Google Drive] Authenticated.\n";
    return true;
}

bool GDriveOAuthProvider::RefreshToken(std::string& err) {
    std::vector<std::pair<std::string, std::string>> form = {
        {"client_id", client_id_},
        {"refresh_token", token_.refresh_token},
        {"grant_type", "refresh_token"}};
    if (!client_secret_.empty())
        form.emplace_back("client_secret", client_secret_);
    std::string resp = PostForm(kTokenUrl, UrlUtil::BuildQuery(form), err);
    if (resp.empty() || !JsonGet(resp, "error").empty())
        return false;
    token_.SetFromResponse(JsonGet(resp, "access_token"), token_.refresh_token,
                           std::max(JsonGetInt(resp, "expires_in"), 60));
    return !token_.access_token.empty();
}

// ─── GDriveServiceAccountAuth ─────────────────────────────────────────────────
GDriveServiceAccountAuth::GDriveServiceAccountAuth(std::string key_json)
    : OAuth2AuthBase("gdrive-sa") {
    // The key file is a flat JSON object.  Parsing it properly also turns the
    // "\n" escapes of private_key into real newlines, which PEM requires.
    auto get = [&](const std::string& k) { return JsonUtil::GetString(key_json, k); };
    project_id_ = get("project_id");
    client_email_ = get("client_email");
    private_key_id_ = get("private_key_id");
    private_key_ = get("private_key");
}

std::string GDriveServiceAccountAuth::SignJWT() const {
    using namespace std::chrono;
    auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();

    std::string header = "{\"alg\":\"RS256\",\"typ\":\"JWT\",\"kid\":\"" + private_key_id_ + "\"}";
    std::string payload = "{\"iss\":\"" + client_email_ +
                          "\""
                          ",\"scope\":\"https://www.googleapis.com/auth/drive\""
                          ",\"aud\":\"https://oauth2.googleapis.com/token\""
                          ",\"iat\":" +
                          std::to_string(now) + ",\"exp\":" + std::to_string(now + 3600) + "}";

    std::string signing_input = GDriveBase64UrlStr(header) + "." + GDriveBase64UrlStr(payload);

    BIO* bio = BIO_new_mem_buf(private_key_.c_str(), static_cast<int>(private_key_.size()));
    if (!bio)
        return "";
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey)
        return "";

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
    std::string body = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
                       "&assertion=" +
                       jwt;
    std::string resp = PostForm("https://oauth2.googleapis.com/token", body, err);
    if (resp.empty())
        return false;
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
