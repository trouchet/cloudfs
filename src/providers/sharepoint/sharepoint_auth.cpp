#include "providers/sharepoint_backend.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace duckdb {

bool SharePointDeviceCodeAuth::AcquireToken(std::string &err) {
    std::string dcode_url = "https://login.microsoftonline.com/" + tenant_id_ + "/oauth2/v2.0/devicecode";
    std::string body      = "client_id=" + client_id_ + "&scope=" + scope_;
    std::string resp      = PostForm(dcode_url, body, err);
    if (resp.empty()) return false;

    std::string device_code = JsonGet(resp, "device_code");
    std::string user_code   = JsonGet(resp, "user_code");
    std::string verify_url  = JsonGet(resp, "verification_uri");
    if (verify_url.empty()) verify_url = JsonGet(resp, "verification_url");
    int expires_in          = JsonGetInt(resp, "expires_in");
    int interval            = std::max(JsonGetInt(resp, "interval"), 5);

    if (device_code.empty()) { err = "no device_code in response: " + resp.substr(0, 200); return false; }

    std::cerr << "\n[cloudfs SharePoint] Authenticate at: " << verify_url
              << "\nCode: " << user_code << "\nWaiting...\n";

    return PollDeviceCode(device_code, interval, expires_in, err);
}

bool SharePointDeviceCodeAuth::PollDeviceCode(const std::string &device_code,
                                               int interval, int expires_in,
                                               std::string &err) {
    std::string token_url = "https://login.microsoftonline.com/" + tenant_id_ + "/oauth2/v2.0/token";
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(expires_in);

    while (std::chrono::system_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        std::string body = "client_id=" + client_id_ +
                           "&device_code=" + device_code +
                           "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
        std::string resp = PostForm(token_url, body, err);
        if (resp.empty()) continue;
        std::string ec = JsonGet(resp, "error");
        if (ec == "authorization_pending") continue;
        if (ec == "slow_down") { interval += 5; continue; }
        if (!ec.empty()) { err = "auth error: " + ec + " — " + JsonGet(resp, "error_description"); return false; }

        std::string at = JsonGet(resp, "access_token");
        if (at.empty()) { err = "no access_token: " + resp.substr(0,200); return false; }
        token_.SetFromResponse(at, JsonGet(resp, "refresh_token"), std::max(JsonGetInt(resp, "expires_in"), 3600));
        std::cerr << "[cloudfs SharePoint] Authenticated.\n";
        return true;
    }
    err = "device code flow timed out";
    return false;
}

bool SharePointDeviceCodeAuth::RefreshToken(std::string &err) {
    std::string url  = "https://login.microsoftonline.com/" + tenant_id_ + "/oauth2/v2.0/token";
    std::string body = "client_id=" + client_id_ +
                       "&refresh_token=" + token_.refresh_token +
                       "&grant_type=refresh_token&scope=" + scope_;
    std::string resp = PostForm(url, body, err);
    if (resp.empty()) return false;
    if (!JsonGet(resp, "error").empty()) { err = "refresh: " + JsonGet(resp, "error"); return false; }
    std::string at = JsonGet(resp, "access_token");
    if (at.empty()) return false;
    token_.SetFromResponse(at, JsonGet(resp, "refresh_token"), std::max(JsonGetInt(resp, "expires_in"), 3600));
    return true;
}

} // namespace duckdb
