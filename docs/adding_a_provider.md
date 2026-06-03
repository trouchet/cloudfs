# How to Add a New Cloud Storage Provider

Adding a provider to `cloudfs` requires touching **zero** framework files. You
implement one C++ class, two optional auth classes, and register everything in a
single `Init()` function. The full `CloudFileSystem`, caching, glob, error
handling, and DuckDB integration continue to work unchanged.

______________________________________________________________________

## Step 1 — Create your backend header

```cpp
// src/include/providers/my_storage_backend.hpp
#pragma once
#include "../core/cloud_backend.hpp"
#include "../core/cloud_http.hpp"

namespace duckdb {

class MyStorageBackend : public ICloudBackend {
public:
    explicit MyStorageBackend(CloudHttpClient &http) : http_(http) {}

    // ── Identity ──────────────────────────────────────────────────────────
    std::string Scheme() const override { return "myfs"; }
    std::string Name()   const override { return "MyStorage"; }

    ProviderCapabilities Capabilities() const override {
        return {
            .supports_range_reads       = true,
            .supports_resumable_uploads = true,
            .upload_chunk_alignment     = 256 * 1024,
        };
    }

    // ── URL parsing ───────────────────────────────────────────────────────
    // myfs://bucket-name/path/to/file.parquet
    bool ParseUrl(const std::string &url,
                  std::string &out_root, std::string &out_path,
                  std::string &err) const override {
        const std::string pfx = "myfs://";
        auto rest  = url.substr(pfx.size());
        auto slash = rest.find('/');
        out_root = rest.substr(0, slash);
        out_path = slash == std::string::npos ? "/" : rest.substr(slash);
        return true;
    }

    // ── Required overrides ────────────────────────────────────────────────
    bool Stat(...) override;
    int64_t ReadRange(...) override;
    bool ListFolder(...) override;
    bool CreateUploadSession(...) override;
    bool UploadChunk(...) override;
    bool DeleteItem(...) override;
    bool CreateFolder(...) override;

private:
    CloudHttpClient &http_;
};
```

## Step 2 — Implement the backend

```cpp
// src/providers/my_storage/my_storage_backend.cpp
#include "providers/my_storage_backend.hpp"

namespace duckdb {

bool MyStorageBackend::Stat(const std::string &root, const std::string &path,
                             const std::string &token,
                             CloudItem &out, std::string &err) {
    HttpRequest req;
    req.url = "https://api.mystorage.io/v1/buckets/" + root + "/objects/" + path;
    req.SetBearerAuth(token);

    auto resp = http_.Execute(req);
    if (!resp.ok()) { err = "Stat failed: " + resp.body; return false; }

    // Parse your API's response into CloudItem fields:
    out.id        = JsonUtil::GetString(resp.body, "id");
    out.name      = JsonUtil::GetString(resp.body, "name");
    out.size      = JsonUtil::GetInt64(resp.body, "size");
    out.etag      = JsonUtil::GetString(resp.body, "etag");
    out.is_folder = JsonUtil::GetBool(resp.body, "isFolder");
    return true;
}

int64_t MyStorageBackend::ReadRange(const CloudItem &item, const std::string &root,
                                     const std::string &token,
                                     int64_t off, int64_t len,
                                     char *buf, std::string &err) {
    // CloudHttpClient::ReadRange handles 206 vs 200 fallback for you:
    std::string download_url = "https://api.mystorage.io/v1/buckets/" + root +
                               "/objects/" + item.id + "/download";
    return http_.ReadRange(download_url, token, off, len, buf, err);
}

// ... implement the remaining methods similarly
} // namespace duckdb
```

## Step 3 — Write an auth provider (optional)

```cpp
class MyStorageApiKeyAuth : public ICloudAuthProvider {
public:
    explicit MyStorageApiKeyAuth(std::string api_key)
        : key_(std::move(api_key)) {}

    bool GetAccessToken(std::string &out, std::string &err) override {
        out = key_;  // API key auth: token IS the key
        return !key_.empty();
    }
    std::string ProviderName() const override { return "mystorage"; }
private:
    std::string key_;
};

// OR: inherit OAuth2AuthBase and implement AcquireToken() + RefreshToken()
```

## Step 4 — Register everything (one place, zero framework changes)

```cpp
// src/extension/cloudfs_extension.cpp  — add inside LoadInternal():

g_cfs->RegisterBackend(make_uniq<MyStorageBackend>(g_cfs->GetHttpClient()));

CloudSecretRegistry::Register(loader, *g_cfs, "mystorage", "apikey",
    {"api_key"},
    [](ClientContext &, CreateSecretInput &in, CloudFileSystem &cfs) {
        auto key = in.options.count("api_key")
                   ? in.options.at("api_key").ToString() : "";
        if (key.empty()) throw InvalidInputException("mystorage: API_KEY required");
        cfs.SetAuth("myfs", make_shared<MyStorageApiKeyAuth>(key));
    });
```

## Step 5 — Use it immediately

```sql
LOAD 'cloudfs';

CREATE SECRET my_store (
    TYPE     mystorage,
    PROVIDER apikey,
    API_KEY  'sk-live-abc123'
);

SELECT * FROM read_parquet('myfs://my-bucket/data/**/*.parquet');
COPY (SELECT ...) TO 'myfs://my-bucket/exports/result.parquet';
```

______________________________________________________________________

## Capability matrix — what to implement vs. what the framework handles

| Task                  | Your code                                 | Framework                         |
| --------------------- | ----------------------------------------- | --------------------------------- |
| URL parsing           | `ParseUrl()`                              | —                                 |
| Auth / token refresh  | `ICloudAuthProvider`                      | Calls your `GetAccessToken()`     |
| File metadata         | `Stat()`                                  | Caches result 15 min              |
| Byte-range reads      | `ReadRange()`                             | Handles fallback, retry, refresh  |
| Pagination            | `ListFolder()` (cursor loop)              | Drives the loop for Glob          |
| Glob `**/*.parquet`   | — (free)                                  | Uses `ListFolder()` + fnmatch     |
| Chunked upload        | `CreateUploadSession()` + `UploadChunk()` | Buffers, aligns, flushes          |
| Error retry           | — (free)                                  | `CloudHttpClient` retries 429/503 |
| Cache invalidation    | — (free)                                  | Automatic on write/delete         |
| DuckDB FileSystem API | — (free)                                  | `CloudFileSystem` implements all  |
| Parquet / CSV / Delta | — (free)                                  | DuckDB format layer, unchanged    |
