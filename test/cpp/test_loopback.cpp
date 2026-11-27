// Unit tests for the OAuth loopback redirect receiver (core/cloud_loopback).
//
// Standalone (no DuckDB): a thread plays the browser and connects to the
// listener over a real loopback socket.
//
//   make -C test/cpp

#include "core/cloud_loopback.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using duckdb::LoopbackCodeReceiver;

static int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            ++g_failures;                                                \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
        }                                                                \
    } while (0)

// Sends one raw request to 127.0.0.1:port and returns the whole response.
static std::string Send(int port, const std::string& raw) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<unsigned short>(port));
    std::string response;
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        (void)!write(fd, raw.data(), raw.size());
        char buf[1024];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            response.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return response;
}

static std::string Get(int port, const std::string& target) {
    return Send(port, "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
}

int main() {
    // ── QueryParam ───────────────────────────────────────────────────────────
    CHECK(LoopbackCodeReceiver::QueryParam("/?code=abc&state=xyz", "code") == "abc");
    CHECK(LoopbackCodeReceiver::QueryParam("/?code=abc&state=xyz", "state") == "xyz");
    CHECK(LoopbackCodeReceiver::QueryParam("/?code=4%2F0Ab-c&state=x", "code") == "4/0Ab-c");
    CHECK(LoopbackCodeReceiver::QueryParam("/?a=1+2", "a") == "1 2");
    CHECK(LoopbackCodeReceiver::QueryParam("/?xcode=no&code=yes", "code") == "yes");
    CHECK(LoopbackCodeReceiver::QueryParam("/?code=abc#frag", "code") == "abc");
    CHECK(LoopbackCodeReceiver::QueryParam("/?code=a%zzb", "code") == "a%zzb");
    CHECK(LoopbackCodeReceiver::QueryParam("/?code=a%4", "code") == "a%4");
    CHECK(LoopbackCodeReceiver::QueryParam("/favicon.ico", "code").empty());
    CHECK(LoopbackCodeReceiver::QueryParam("/?state=x", "code").empty());

    std::string err;

    // ── Listening ────────────────────────────────────────────────────────────
    {
        LoopbackCodeReceiver r;
        CHECK(r.Listen(0, err));
        CHECK(r.Port() > 0);
        CHECK(r.RedirectUri() == "http://127.0.0.1:" + std::to_string(r.Port()));
        // The same fixed port cannot be bound twice.
        LoopbackCodeReceiver second;
        std::string second_err;
        CHECK(!second.Listen(r.Port(), second_err));
        CHECK(second_err.find("in use") != std::string::npos);
        CHECK(!second.Listen(70000, second_err));
    }
    {
        LoopbackCodeReceiver r;
        std::string code;
        CHECK(!r.WaitForCode("s", 1, code, err)); // not listening
    }

    // ── Happy path: unrelated and forged requests are ignored first ──────────
    {
        LoopbackCodeReceiver r;
        CHECK(r.Listen(0, err));
        int port = r.Port();
        std::string favicon, forged, post, ok;
        std::thread browser([&] {
            favicon = Get(port, "/favicon.ico");
            forged = Get(port, "/?code=evil&state=wrong");
            post = Send(port, "POST /?code=evil&state=good HTTP/1.1\r\n\r\n");
            ok = Get(port, "/?state=good&code=4%2F0Ab-c&scope=x");
        });
        std::string code;
        bool got = r.WaitForCode("good", 20, code, err);
        browser.join();
        CHECK(got);
        CHECK(code == "4/0Ab-c");
        CHECK(favicon.compare(0, 12, "HTTP/1.1 404") == 0);
        CHECK(forged.compare(0, 12, "HTTP/1.1 404") == 0);
        CHECK(post.compare(0, 12, "HTTP/1.1 404") == 0);
        CHECK(ok.compare(0, 12, "HTTP/1.1 200") == 0);
        // Fixed pages only: nothing from the request is echoed back.
        CHECK(forged.find("evil") == std::string::npos);
        CHECK(ok.find("4/0Ab-c") == std::string::npos);
    }

    // ── The user declined ────────────────────────────────────────────────────
    {
        LoopbackCodeReceiver r;
        CHECK(r.Listen(0, err));
        int port = r.Port();
        std::thread browser([&] { Get(port, "/?error=access_denied&state=good"); });
        std::string code;
        err.clear();
        CHECK(!r.WaitForCode("good", 20, code, err));
        browser.join();
        CHECK(err.find("access_denied") != std::string::npos);
        CHECK(code.empty());
    }

    // ── Nobody comes back ────────────────────────────────────────────────────
    {
        LoopbackCodeReceiver r;
        CHECK(r.Listen(0, err));
        std::string code;
        err.clear();
        auto start = std::chrono::steady_clock::now();
        CHECK(!r.WaitForCode("good", 1, code, err));
        auto waited = std::chrono::steady_clock::now() - start;
        CHECK(err.find("timed out") != std::string::npos);
        CHECK(waited < std::chrono::seconds(5));
    }

    if (g_failures) {
        std::fprintf(stderr, "test_loopback: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("test_loopback: all checks passed\n");
    return 0;
}
