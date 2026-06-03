#include "providers/dropbox_backend.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace duckdb {

bool DropboxOAuthProvider::AcquireToken(std::string& err) {
    // Dropbox uses PKCE but also supports the offline token flow.
    // For CLI/server use, Dropbox recommends the offline token approach:
    // Users authorize via https://www.dropbox.com/oauth2/authorize?... in their browser
    // then paste the code.  We simulate a device-code-like UX.
    std::cerr << "\n[cloudfs Dropbox] To authenticate:\n"
              << "1. Visit: https://www.dropbox.com/oauth2/authorize"
                 "?client_id="
              << app_key_ << "&token_access_type=offline&response_type=code\n"
              << "2. Authorize the app\n"
              << "3. Paste the authorization code below and press Enter:\n> ";

    std::string code;
    if (!std::getline(std::cin, code) || code.empty()) {
        err = "No authorization code provided";
        return false;
    }

    std::string body = "code=" + code +
                       "&grant_type=authorization_code"
                       "&client_id=" +
                       app_key_ + "&client_secret=" + app_secret_;
    std::string resp = PostForm(kTokenUrl, body, err);
    if (resp.empty())
        return false;
    if (!JsonGet(resp, "error").empty()) {
        err = JsonGet(resp, "error");
        return false;
    }

    token_.SetFromResponse(JsonGet(resp, "access_token"), JsonGet(resp, "refresh_token"),
                           std::max(JsonGetInt(resp, "expires_in"), 14400));
    std::cerr << "[cloudfs Dropbox] Authenticated.\n";
    return !token_.access_token.empty();
}

bool DropboxOAuthProvider::RefreshToken(std::string& err) {
    std::string body = "refresh_token=" + token_.refresh_token +
                       "&grant_type=refresh_token"
                       "&client_id=" +
                       app_key_ + "&client_secret=" + app_secret_;
    std::string resp = PostForm(kTokenUrl, body, err);
    if (resp.empty() || !JsonGet(resp, "error").empty())
        return false;
    token_.SetFromResponse(JsonGet(resp, "access_token"), token_.refresh_token,
                           std::max(JsonGetInt(resp, "expires_in"), 14400));
    return !token_.access_token.empty();
}

} // namespace duckdb
