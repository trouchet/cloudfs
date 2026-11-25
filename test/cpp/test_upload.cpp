// Tests for the write path of CloudFileSystem, driven through an in-memory
// backend that requires the total size up front (the spooled upload path used
// by OneDrive and SharePoint): chunk alignment, sequential-only writes, abort
// on failure, empty files and destruction without Close().
//
// Needs a finished release build:  make release && make -C test/cpp upload

#include "duckdb/main/extension/extension_loader.hpp"

#include "core/cloud_filesystem.hpp"

#include "duckdb.hpp"

#include <iostream>
using namespace duckdb;

struct FakeBackend : ICloudBackend {
    std::string data;
    std::vector<std::string> log;
    int fail_at_chunk = -1;
    int chunks = 0;
    bool aborted = false;
    int64_t session_total = -2;
    std::string Scheme() const override { return "fake"; }
    std::string SecretType() const override { return "vfs"; }
    std::string Name() const override { return "fake"; }
    ProviderCapabilities Capabilities() const override {
        ProviderCapabilities c;
        c.needs_total_size_upfront = true;
        c.upload_chunk_alignment = 320 * 1024;
        c.min_upload_chunk = 320 * 1024;
        c.max_upload_chunk = 60 * 1024 * 1024;
        return c;
    }
    bool ParseUrl(const std::string& url, std::string& r, std::string& p,
                  std::string&) const override {
        r = "r";
        p = url.substr(7);
        return true;
    }
    bool Stat(const std::string&, const std::string& path, const std::string&, CloudItem& out,
              std::string& err) override {
        if (path == "/" || path == "/dir") {
            out.id = "dir";
            out.is_folder = true;
            return true;
        }
        err = CloudError::NotFound(path);
        return false;
    }
    int64_t ReadRange(const CloudItem&, const std::string&, const std::string&, int64_t, int64_t,
                      char*, std::string&) override {
        return -1;
    }
    bool ListFolder(const std::string&, const std::string&, const std::string&,
                    const std::function<void(const CloudItem&)>&, std::string&,
                    std::string&) override {
        return true;
    }
    bool CreateUploadSession(const std::string&, const std::string&, const std::string&,
                             int64_t total, const std::string&, CloudUploadSession& out,
                             std::string&) override {
        session_total = total;
        out.upload_url = "u";
        out.chunk_size_bytes = 1000 * 1000;
        return true;
    }
    bool UploadChunk(const CloudUploadSession& s, const char* d, int64_t off, int64_t sz, bool last,
                     const std::string&, std::string& err) override {
        if (chunks++ == fail_at_chunk) {
            err = "boom";
            return false;
        }
        if (s.total_size_bytes != session_total) {
            err = "total changed";
            return false;
        }
        if (off != (int64_t)data.size()) {
            err = "gap";
            return false;
        }
        if (!last && sz % (320 * 1024) != 0) {
            err = "unaligned";
            return false;
        }
        data.append(d, (size_t)sz);
        log.push_back(std::to_string(sz) + (last ? "L" : ""));
        return true;
    }
    bool AbortUpload(const CloudUploadSession&, const std::string&, std::string&) override {
        aborted = true;
        return true;
    }
    bool DeleteItem(const std::string&, const std::string&, const std::string&,
                    std::string&) override {
        return false;
    }
    bool CreateFolder(const std::string&, const std::string&, const std::string&,
                      const std::string&, CloudItem&, std::string&) override {
        return false;
    }
    bool MoveItem(const std::string&, const CloudItem&, const CloudItem&, const std::string&,
                  const std::string&, std::string&) override {
        return false;
    }
};
static int fails = 0;
#define CHECK(c)                                                   \
    do {                                                           \
        if (!(c)) {                                                \
            ++fails;                                               \
            std::cout << "FAIL line " << __LINE__ << ": " #c "\n"; \
        }                                                          \
    } while (0)

int main() {
    DuckDB db(nullptr);
    Connection con(db);
    auto cfs_u = make_uniq<CloudFileSystem>(*db.instance);
    auto* cfs = cfs_u.get();
    auto fb_u = make_uniq<FakeBackend>();
    auto* fb = fb_u.get();
    cfs->RegisterBackend(std::move(fb_u));
    // The build links cloudfs statically, so the "vfs" secret type exists; add
    // a secret of that type and a factory on this test's own registry.
    auto& sm = SecretManager::Get(*db.instance);
    auto secret = make_uniq<KeyValueSecret>(vector<string>{}, "vfs", "token", "s");
    secret->secret_map["token"] = Value("tok");
    sm.RegisterSecret(CatalogTransaction::GetSystemTransaction(*db.instance), std::move(secret),
                      OnCreateConflict::ERROR_ON_CONFLICT, SecretPersistType::TEMPORARY);
    cfs->GetSecrets().AddFactory(
        "vfs", "token", [](const SecretParams& p) -> std::shared_ptr<ICloudAuthProvider> {
            return std::make_shared<StaticTokenAuth>("fake", p.Get("token"));
        });

    std::string payload;
    payload.reserve(2500000);
    for (int i = 0; i < 2500000; ++i)
        payload += (char)('a' + i % 26);
    { // 1. spooled upload, written in odd-sized pieces
        auto h = cfs->OpenFile("fake:///dir/a.bin",
                               FileFlags::FILE_FLAGS_WRITE | FileFlags::FILE_FLAGS_FILE_CREATE);
        size_t off = 0;
        for (size_t n : {7ul, 1000000ul, 333ul, 1499660ul}) {
            cfs->Write(*h, (void*)(payload.data() + off), (int64_t)n);
            off += n;
        }
        CHECK(off == payload.size());
        CHECK(fb->data.empty()); // nothing sent before close
        CHECK(cfs->GetFileSize(*h) == (int64_t)payload.size());
        h->Close();
        CHECK(fb->data == payload);
        CHECK(fb->session_total == (int64_t)payload.size());
        std::cout << "chunks:";
        for (auto& l : fb->log)
            std::cout << " " << l;
        std::cout << "\n";
        CHECK(fb->log.size() == 3); // 2 x 983040 (aligned) + remainder
        h->Close();
        CHECK(fb->log.size() == 3); // idempotent
    }
    { // 2. non-sequential write is rejected
        fb->data.clear();
        fb->log.clear();
        fb->chunks = 0;
        auto h = cfs->OpenFile("fake:///dir/b.bin", FileFlags::FILE_FLAGS_WRITE);
        cfs->Write(*h, (void*)payload.data(), 10);
        bool threw = false;
        try {
            cfs->Write(*h, (void*)payload.data(), 10, 100);
        } catch (std::exception& e) {
            threw = std::string(e.what()).find("non-sequential") != std::string::npos;
        }
        CHECK(threw);
    }
    { // 3. failing chunk: Close throws, session aborted, destructor quiet
        fb->data.clear();
        fb->log.clear();
        fb->chunks = 0;
        fb->fail_at_chunk = 1;
        fb->aborted = false;
        auto h = cfs->OpenFile("fake:///dir/c.bin", FileFlags::FILE_FLAGS_WRITE);
        cfs->Write(*h, (void*)payload.data(), (int64_t)payload.size());
        bool threw = false;
        try {
            h->Close();
        } catch (std::exception& e) {
            threw = std::string(e.what()).find("boom") != std::string::npos;
        }
        CHECK(threw);
        CHECK(fb->aborted);
        h.reset(); // must not throw or retry
        CHECK(fb->chunks == 2);
    }
    { // 4. empty file
        fb->data.clear();
        fb->log.clear();
        fb->chunks = 0;
        fb->fail_at_chunk = -1;
        auto h = cfs->OpenFile("fake:///dir/d.bin", FileFlags::FILE_FLAGS_WRITE);
        h->Close();
        CHECK(fb->log.size() == 1 && fb->log[0] == "0L");
        CHECK(fb->session_total == 0);
    }
    { // 5. destructor without Close completes the upload
        fb->data.clear();
        fb->log.clear();
        fb->chunks = 0;
        auto h = cfs->OpenFile("fake:///dir/e.bin", FileFlags::FILE_FLAGS_WRITE);
        cfs->Write(*h, (void*)payload.data(), 100);
        h.reset();
        CHECK(fb->data.size() == 100);
    }
    std::cout << (fails ? "FAILED" : "test_upload: all checks passed") << "\n";
    return fails ? 1 : 0;
}
