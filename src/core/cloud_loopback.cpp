#include "core/cloud_loopback.hpp"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_fd_t = SOCKET;
using socklen_type = int;
static const socket_fd_t kInvalidSocket = INVALID_SOCKET;
static void CloseSocket(socket_fd_t fd) {
    closesocket(fd);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_fd_t = int;
using socklen_type = socklen_t;
static const socket_fd_t kInvalidSocket = -1;
static void CloseSocket(socket_fd_t fd) {
    ::close(fd);
}
#endif

namespace duckdb {

namespace {

using Clock = std::chrono::steady_clock;

// A request line plus headers of a browser redirect is far below this.
const size_t kMaxRequestBytes = 16 * 1024;
// How long one connection may take to send its request.
const int kRequestTimeoutMs = 5000;

// True when `fd` becomes readable within `timeout_ms`.
bool WaitReadable(socket_fd_t fd, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(static_cast<int>(fd) + 1, &set, nullptr, nullptr, &tv) > 0;
}

// Reads until the end of the HTTP headers and returns the request target of a
// GET request ("" for anything else).
std::string ReadRequestTarget(socket_fd_t fd) {
    std::string request;
    auto deadline = Clock::now() + std::chrono::milliseconds(kRequestTimeoutMs);
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < kMaxRequestBytes) {
        auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0 || !WaitReadable(fd, static_cast<int>(left)))
            break;
        char buf[2048];
        auto n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        request.append(buf, static_cast<size_t>(n));
    }
    auto line_end = request.find("\r\n");
    if (line_end == std::string::npos || request.compare(0, 4, "GET ") != 0)
        return "";
    auto target_end = request.find(' ', 4);
    if (target_end == std::string::npos || target_end > line_end)
        return "";
    return request.substr(4, target_end - 4);
}

void Respond(socket_fd_t fd, const char* status, const std::string& message) {
    std::string body = "<!doctype html><html><head><meta charset=\"utf-8\">"
                       "<title>cloudfs</title></head><body style=\"font-family:sans-serif\">"
                       "<h3>cloudfs</h3><p>" +
                       message + "</p></body></html>";
    std::string response = std::string("HTTP/1.1 ") + status +
                           "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                           std::to_string(body.size()) +
                           "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < response.size()) {
        auto n = send(fd, response.data() + sent, static_cast<int>(response.size() - sent), 0);
        if (n <= 0)
            break;
        sent += static_cast<size_t>(n);
    }
}

int HexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string PercentDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '+') {
            out += ' ';
        } else if (in[i] == '%' && i + 2 < in.size() && HexValue(in[i + 1]) >= 0 &&
                   HexValue(in[i + 2]) >= 0) {
            out += static_cast<char>(HexValue(in[i + 1]) * 16 + HexValue(in[i + 2]));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

} // namespace

std::string LoopbackCodeReceiver::QueryParam(const std::string& target, const std::string& key) {
    auto q = target.find('?');
    if (q == std::string::npos)
        return "";
    auto end = target.find('#', q);
    if (end == std::string::npos)
        end = target.size();
    size_t pos = q + 1;
    while (pos < end) {
        auto amp = target.find('&', pos);
        if (amp == std::string::npos || amp > end)
            amp = end;
        auto eq = target.find('=', pos);
        if (eq != std::string::npos && eq < amp && target.compare(pos, eq - pos, key) == 0)
            return PercentDecode(target.substr(eq + 1, amp - eq - 1));
        pos = amp + 1;
    }
    return "";
}

LoopbackCodeReceiver::~LoopbackCodeReceiver() {
    Close();
}

void LoopbackCodeReceiver::Close() {
    if (fd_ != -1) {
        CloseSocket(static_cast<socket_fd_t>(fd_));
        fd_ = -1;
    }
}

bool LoopbackCodeReceiver::Listen(int port, std::string& err) {
    Close();
    if (port < 0 || port > 65535) {
        err = "invalid redirect port " + std::to_string(port);
        return false;
    }
    socket_fd_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) {
        err = "cannot create a local socket for the OAuth redirect";
        return false;
    }
#ifndef _WIN32
    // Lets a fixed port be reused right after an earlier attempt.  Not set on
    // Windows, where the option would allow another process to share the port.
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<unsigned short>(port));
    socklen_type len = sizeof(addr);
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), len) != 0 || listen(fd, 8) != 0 ||
        getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        CloseSocket(fd);
        err = "cannot listen on 127.0.0.1:" + std::to_string(port) + " for the OAuth redirect" +
              (port ? " (is the port in use?)" : "");
        return false;
    }
    fd_ = static_cast<intptr_t>(fd);
    port_ = ntohs(addr.sin_port);
    return true;
}

std::string LoopbackCodeReceiver::RedirectUri() const {
    return "http://127.0.0.1:" + std::to_string(port_);
}

bool LoopbackCodeReceiver::WaitForCode(const std::string& expected_state, int timeout_seconds,
                                       std::string& code, std::string& err) {
    if (fd_ == -1) {
        err = "the OAuth redirect listener is not running";
        return false;
    }
    auto listener = static_cast<socket_fd_t>(fd_);
    auto deadline = Clock::now() + std::chrono::seconds(timeout_seconds);
    while (Clock::now() < deadline) {
        if (!WaitReadable(listener, 500))
            continue;
        socket_fd_t conn = accept(listener, nullptr, nullptr);
        if (conn == kInvalidSocket)
            continue;
        std::string target = ReadRequestTarget(conn);

        // Browsers also ask for /favicon.ico and the like; anything that does
        // not prove it comes from our authorization request is ignored.
        if (target.empty() || expected_state.empty() ||
            QueryParam(target, "state") != expected_state) {
            Respond(conn, "404 Not Found", "Nothing to see here.");
            CloseSocket(conn);
            continue;
        }

        std::string error = QueryParam(target, "error");
        if (!error.empty()) {
            Respond(conn, "200 OK",
                    "Authorization was not granted. You can close this tab and return to DuckDB.");
            CloseSocket(conn);
            err = "authorization was not granted: " + error;
            return false;
        }
        code = QueryParam(target, "code");
        if (code.empty()) {
            Respond(conn, "400 Bad Request", "The authorization response carried no code.");
            CloseSocket(conn);
            continue;
        }
        Respond(conn, "200 OK",
                "Authorization received. You can close this tab and return to DuckDB.");
        CloseSocket(conn);
        return true;
    }
    err = "timed out waiting for the authorization redirect on " + RedirectUri();
    return false;
}

} // namespace duckdb
