// Unit tests for the request-building helpers in core/cloud_http.
//
// Standalone on purpose (no DuckDB, no test framework): the helpers only need
// libcurl, so this builds in a second and can run anywhere.
//
//   make -C test/cpp        (or see test/cpp/Makefile)

#include "core/cloud_http.hpp"

#include <cstdio>
#include <string>

using duckdb::JsonUtil;
using duckdb::UrlUtil;

static int g_failures = 0;

static void Check(const char* what, const std::string& got, const std::string& want) {
    if (got == want)
        return;
    ++g_failures;
    std::fprintf(stderr, "FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
}

int main() {
    // ── JsonUtil::Quote ──────────────────────────────────────────────────────
    Check("quote plain", JsonUtil::Quote("a b.csv"), "\"a b.csv\"");
    Check("quote double quote", JsonUtil::Quote("q\"uote.csv"), "\"q\\\"uote.csv\"");
    Check("quote backslash", JsonUtil::Quote("a\\b"), "\"a\\\\b\"");
    Check("quote control", JsonUtil::Quote("a\nb\x01"), "\"a\\nb\\u0001\"");
    Check("quote keeps utf-8", JsonUtil::Quote("a\xC3\xA7\xC3\xA3o"), "\"a\xC3\xA7\xC3\xA3o\"");
    // A name that tries to close the string and add a field stays one string.
    Check("quote injection", JsonUtil::Quote("x\",\"mode\":\"overwrite"),
          "\"x\\\",\\\"mode\\\":\\\"overwrite\"");

    // ── JsonUtil::QuoteAscii (JSON inside an HTTP header) ────────────────────
    Check("ascii plain", JsonUtil::QuoteAscii("/a b.csv"), "\"/a b.csv\"");
    Check("ascii 2-byte", JsonUtil::QuoteAscii("a\xC3\xA7\xC3\xA3o"), "\"a\\u00e7\\u00e3o\"");
    Check("ascii 3-byte", JsonUtil::QuoteAscii("\xE2\x82\xAC"), "\"\\u20ac\"");
    Check("ascii surrogate pair", JsonUtil::QuoteAscii("\xF0\x9F\x98\x80"), "\"\\ud83d\\ude00\"");
    Check("ascii DEL", JsonUtil::QuoteAscii("a\x7F"), "\"a\\u007f\"");
    Check("ascii quote+utf8", JsonUtil::QuoteAscii("\"\xC3\xA9"), "\"\\\"\\u00e9\"");
    Check("ascii malformed", JsonUtil::QuoteAscii("a\xC3"), "\"a\\ufffd\"");
    Check("ascii stray continuation", JsonUtil::QuoteAscii("\x80z"), "\"\\ufffdz\"");

    // ── UrlUtil::Encode / EncodePath ─────────────────────────────────────────
    Check("encode reserved", UrlUtil::Encode("50%#&.csv"), "50%25%23%26.csv");
    Check("encode quote", UrlUtil::Encode("it's"), "it%27s");
    Check("path keeps slashes", UrlUtil::EncodePath("/Shared Documents/a#b.csv"),
          "/Shared%20Documents/a%23b.csv");
    Check("path root", UrlUtil::EncodePath("/"), "/");
    Check("path empty", UrlUtil::EncodePath(""), "");
    Check("path no leading slash", UrlUtil::EncodePath("a/b c"), "a/b%20c");
    Check("path trailing slash", UrlUtil::EncodePath("/a b/"), "/a%20b/");
    Check("path percent", UrlUtil::EncodePath("/100%/x"), "/100%25/x");
    Check("path utf-8", UrlUtil::EncodePath("/a\xC3\xA7\xC3\xA3o"), "/a%C3%A7%C3%A3o");
    Check("path question mark", UrlUtil::EncodePath("/a?b=c"), "/a%3Fb%3Dc");

    // ── UrlUtil::BuildQuery ──────────────────────────────────────────────────
    Check("query", UrlUtil::BuildQuery({{"q", "name = 'it\\'s' and trashed = false"}, {"n", "1"}}),
          "q=name%20%3D%20%27it%5C%27s%27%20and%20trashed%20%3D%20false&n=1");

    // ── JsonUtil::GetString: escapes in responses ────────────────────────────
    Check("get \\u0026", JsonUtil::GetString("{\"name\":\"a\\u0026b.csv\"}", "name"), "a&b.csv");
    Check("get \\u00e7", JsonUtil::GetString("{\"name\":\"a\\u00E7\"}", "name"), "a\xC3\xA7");
    Check("get surrogate pair", JsonUtil::GetString("{\"name\":\"\\ud83d\\ude00\"}", "name"),
          "\xF0\x9F\x98\x80");
    Check("get escaped quote", JsonUtil::GetString("{\"name\":\"q\\\"x\"}", "name"), "q\"x");
    Check("get round trip",
          JsonUtil::GetString(
              "{\"name\":" + JsonUtil::QuoteAscii("it's 50%#& \xC3\xB1 \"q\"") + "}", "name"),
          "it's 50%#& \xC3\xB1 \"q\"");

    // ── JsonUtil: reading ────────────────────────────────────────────────────
    // A nested "id" that comes first in the text must not shadow the real one.
    const std::string item = R"({"createdBy":{"user":{"id":"NESTED"}},"id":"TOP","name":"n",)"
                             R"("size":"12345678901","folder":{"childCount":2},"ok":true,"n":7})";
    Check("top-level key wins", JsonUtil::GetString(item, "id"), "TOP");
    Check("numeric string", std::to_string(JsonUtil::GetInt64(item, "size")), "12345678901");
    Check("number", std::to_string(JsonUtil::GetInt64(item, "n")), "7");
    Check("number as text", JsonUtil::GetString(item, "n"), "7");
    Check("bool", JsonUtil::GetBool(item, "ok") ? "t" : "f", "t");
    Check("missing bool", JsonUtil::GetBool(item, "nope") ? "t" : "f", "f");
    Check("has object", JsonUtil::HasObject(item, "folder") ? "t" : "f", "t");
    Check("has object: scalar", JsonUtil::HasObject(item, "name") ? "t" : "f", "f");
    Check("has object: missing", JsonUtil::HasObject(item, "file") ? "t" : "f", "f");
    Check("object as text is non-empty", JsonUtil::GetString(item, "folder").empty() ? "e" : "x",
          "x");
    Check("missing key", JsonUtil::GetString(item, "nope"), "");
    Check("null value", JsonUtil::GetString(R"({"a":null})", "a"), "");
    Check("invalid json", JsonUtil::GetString("{\"a\":", "a"), "");
    Check("not an object", JsonUtil::GetString("[1,2]", "a"), "");
    // A key name inside a string value is not a key.
    Check("key inside string", JsonUtil::GetString(R"({"note":"\"id\":\"x\"","id":"real"})", "id"),
          "real");

    // Arrays: braces and brackets inside names must not break item splitting.
    const std::string listing =
        R"({"files":[{"id":"1","name":"a}b]{.csv"},{"id":"2","name":"q\"x"}],"nextPageToken":"t"})";
    auto files = JsonUtil::GetArray(listing, "files");
    Check("array size", std::to_string(files.size()), "2");
    if (files.size() == 2) {
        Check("array item 0 name", JsonUtil::GetString(files[0], "name"), "a}b]{.csv");
        Check("array item 1 name", JsonUtil::GetString(files[1], "name"), "q\"x");
        Check("array item 1 id", JsonUtil::GetString(files[1], "id"), "2");
    }
    Check("sibling of array", JsonUtil::GetString(listing, "nextPageToken"), "t");
    Check("missing array", std::to_string(JsonUtil::GetArray(listing, "nope").size()), "0");
    auto parents = JsonUtil::GetStringArray(R"({"parents":["p1","p\"2"]})", "parents");
    Check("string array size", std::to_string(parents.size()), "2");
    if (parents.size() == 2)
        Check("string array item", parents[1], "p\"2");

    // PEM newlines survive (service account key files).
    Check("escaped newline",
          JsonUtil::GetString(R"({"private_key":"-----BEGIN-----\nAB\n"})", "private_key"),
          "-----BEGIN-----\nAB\n");

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("test_http_util: all checks passed");
    return 0;
}
