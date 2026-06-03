#include "providers/onedrive_backend.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace duckdb {

bool OneDriveAuth::AcquireToken(std::string& err) {
    std::string dcode_url =
        "https://login.microsoftonline.com/" + tenant_id_ + "/oauth2/v2.0/devicecode";
    std::string body = "client_id=" + client_id_ + "&scope=" + std::string(kScope);
    std::string resp = PostForm(dcode_url, body, err);
    if (resp.empty())
        return false;

    std::string device_code = JsonGet(resp, "device_code");
    std::string user_code = JsonGet(resp, "user_code");
    std::string verify_url = JsonGet(resp, "verification_uri");
    if (verify_url.empty())
        verify_url = JsonGet(resp, "verification_url");
    int expires_in = JsonGetInt(resp, "expires_in");
    int interval = std::max(JsonGetInt(resp, "interval"), 5);

    if (device_code.empty()) {
        err = "no device_code";
        return false;
    }
    std::cerr << "\n[cloudfs OneDrive] Authenticate at: " << verify_url << "\nCode: " << user_code
              << "\n";

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(expires_in);
    std::string token_url =
        "https://login.microsoftonline.com/" + tenant_id_ + "/oauth2/v2.0/token";
    while (std::chrono::system_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        std::string b = "client_id=" + client_id_ + "&device_code=" + device_code +
                        "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
        std::string r = PostForm(token_url, b, err);
        if (r.empty())
            continue;
        std::string ec = JsonGet(r, "error");
        if (ec == "authorization_pending")
            continue;
        if (!ec.empty()) {
            err = ec;
            return false;
        }
        std::string at = JsonGet(r, "access_token");
        if (at.empty())
            continue;
        token_.SetFromResponse(at, JsonGet(r, "refresh_token"),
                               std::max(JsonGetInt(r, "expires_in"), 3600));
        std::cerr << "[cloudfs OneDrive] Authenticated.\n";
        return true;
    }
    err = "timed out";
    return false;
}

bool OneDriveAuth::RefreshToken(std::string& err) {
    std::string url = "https://login.microsoftonline.com/" + tenant_id_ + "/oauth2/v2.0/token";
    std::string body = "client_id=" + client_id_ + "&refresh_token=" + token_.refresh_token +
                       "&grant_type=refresh_token&scope=" + std::string(kScope);
    std::string resp = PostForm(url, body, err);
    if (resp.empty() || !JsonGet(resp, "error").empty())
        return false;
    token_.SetFromResponse(JsonGet(resp, "access_token"), JsonGet(resp, "refresh_token"),
                           std::max(JsonGetInt(resp, "expires_in"), 3600));
    return !token_.access_token.empty();
}

} // namespace duckdb
