#pragma once
#include <cstdint>
#include <string>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// LoopbackCodeReceiver
//
// The receiving end of an OAuth 2.0 "loopback IP address" redirect (RFC 8252
// §7.3): a one-shot HTTP listener bound to 127.0.0.1 that waits for the
// browser to come back with `?code=...&state=...` after the user authorized
// the application.
//
// It only ever listens on the loopback interface, answers with fixed pages
// (nothing from the request is echoed back) and ignores every request that
// does not carry the expected `state`.
// ─────────────────────────────────────────────────────────────────────────────
class LoopbackCodeReceiver {
  public:
    LoopbackCodeReceiver() = default;
    ~LoopbackCodeReceiver();
    LoopbackCodeReceiver(const LoopbackCodeReceiver&) = delete;
    LoopbackCodeReceiver& operator=(const LoopbackCodeReceiver&) = delete;

    // Starts listening on 127.0.0.1.  `port` 0 picks any free port.
    bool Listen(int port, std::string& err);

    // The port actually bound (valid after Listen succeeded).
    int Port() const { return port_; }

    // "http://127.0.0.1:<port>", the redirect_uri to hand to the provider.
    std::string RedirectUri() const;

    // Blocks until a redirect carrying `expected_state` arrives, or until
    // `timeout_seconds` passed.  On success `code` holds the authorization
    // code.  A redirect with `error=...` (the user declined) fails at once.
    bool WaitForCode(const std::string& expected_state, int timeout_seconds, std::string& code,
                     std::string& err);

    void Close();

    // Value of `key` in an HTTP request target such as "/?code=a%2Fb&state=x",
    // percent-decoded; "" when absent.  Exposed for tests.
    static std::string QueryParam(const std::string& target, const std::string& key);

  private:
    intptr_t fd_ = -1;
    int port_ = 0;
};

} // namespace duckdb
