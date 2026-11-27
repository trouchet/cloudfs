// Tests for the pieces of the Google Drive desktop OAuth flow that do not need
// Google: the PKCE challenge, the random tokens and the authorization URL.
//
// Needs a finished release build:  make release && make -C test/cpp oauth

#include "providers/gdrive_backend.hpp"

#include <iostream>
#include <string>

using namespace duckdb;

static int g_failures = 0;
#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        }                                                             \
    } while (0)

int main() {
    // RFC 7636, appendix B (public test vector, not a credential).
    const char* rfc_verifier =
        "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"; // pragma: allowlist secret
    const char* rfc_challenge =
        "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"; // pragma: allowlist secret
    CHECK(GDriveOAuthProvider::PkceChallenge(rfc_verifier) == rfc_challenge);

    // 32 random bytes give a 43-character verifier (the minimum Google accepts)
    // made only of unreserved characters.
    auto verifier = GDriveOAuthProvider::RandomUrlToken(32);
    CHECK(verifier.size() == 43);
    const std::string unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" // pragma: allowlist secret
        "0123456789-_";
    CHECK(verifier.find_first_not_of(unreserved) == std::string::npos);
    CHECK(verifier != GDriveOAuthProvider::RandomUrlToken(32));

    auto url = GDriveOAuthProvider::BuildAuthUrl("id 1.apps.googleusercontent.com",
                                                 "http://127.0.0.1:9004", "st&ate", "chal");
    CHECK(url.rfind("https://accounts.google.com/o/oauth2/v2/auth?", 0) == 0);
    CHECK(url.find("client_id=id%201.apps.googleusercontent.com") != std::string::npos);
    CHECK(url.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A9004") != std::string::npos);
    CHECK(url.find("response_type=code") != std::string::npos);
    CHECK(url.find("scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive&") != std::string::npos);
    CHECK(url.find("code_challenge=chal") != std::string::npos);
    CHECK(url.find("code_challenge_method=S256") != std::string::npos);
    CHECK(url.find("state=st%26ate") != std::string::npos);

    if (g_failures) {
        std::cout << "test_gdrive_oauth: " << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_gdrive_oauth: all checks passed\n";
    return 0;
}
