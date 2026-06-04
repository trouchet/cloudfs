# Security and Robustness Improvements

This document summarizes the security and robustness improvements made to the
cloudfs extension.

## ✅ Completed Improvements

### 1. Hardened Transport and Request Lifecycles

**Changes:**

- Added `CURLOPT_SSL_VERIFYHOST=2` to verify hostname matches certificate
- Added `CURLOPT_CONNECTTIMEOUT_MS` for separate connection timeout (30s)
- Added `CURLOPT_NOSIGNAL` for thread-safe operation
- Added retry jitter (±20%) to prevent thundering herd on retries

**Impact:** Prevents man-in-the-middle attacks by verifying SSL hostname, adds
predictable timeout behavior, and avoids retry storms.

**Files Modified:**

- `src/core/cloud_http.cpp`

### 2. URL and JSON Encoding to Prevent Injection

**Changes:**

- Added `UrlUtil::Encode()` using libcurl's `curl_easy_escape`
- Added `UrlUtil::BuildQuery()` for safe query string construction
- Added `JsonUtil::EscapeJsonString()` for proper JSON string escaping
- Replaced all VFS backend URL/JSON concatenation with safe builders

**Impact:** Prevents injection vulnerabilities from unencoded path/filename
parameters. Filenames containing `&`, `?`, quotes, or backslashes can no longer
mutate the protocol.

**Files Modified:**

- `src/include/core/cloud_http.hpp`
- `src/core/cloud_http.cpp`
- `src/providers/vfs/vfs_backend.cpp`

### 3. Fixed Agent's Shared State with Mutex and Upload Limits

**Changes:**

- Added `sync.RWMutex` to protect `uploadSessions` map from concurrent access
- Added `BytesWritten` tracking to enforce max upload size during streaming
- Enforce `flagMaxSize` limit at upload start, during chunks, and at finish
- Added background cleanup goroutine to remove stale sessions (>1 hour)
- Return HTTP 413 when upload exceeds configured size limit

**Impact:** Prevents race conditions in concurrent uploads, prevents unbounded
memory/disk usage, and cleans up abandoned sessions.

**Files Modified:**

- `agent/main.go`

### 4. Made Partial Reads Strict

**Changes:**

- Expect HTTP 206 for range reads (not 200)
- Only accept 200 if requesting from offset 0 (safe fallback)
- Reject 200 responses for non-zero offsets to prevent feeding DuckDB wrong data
- Add clear error message when server ignores Range header

**Impact:** Prevents data corruption bug where server ignoring Range header
could feed DuckDB the wrong slice of data.

**Files Modified:**

- `src/core/cloud_http.cpp`

### 6. Fixed Cache Metadata Handling

**Changes:**

- Fix `GetLastModifiedTime` to use `item.modified_time_ms` from cache
- Convert Unix milliseconds to DuckDB timestamp using `FromEpochMs`
- Fallback to current time only if modification time unavailable
- Add comment about O(n) cache eviction being acceptable for small caches (512
  items)

**Impact:** Fixes metadata correctness - file modification times are now
accurate instead of always "now", which prevented unnecessary refresh behavior.

**Files Modified:**

- `src/core/cloud_filesystem.cpp`
- `src/core/cloud_cache.cpp`

### 7. Documented Secret Handling Best Practices

**Changes:**

- Added warning in README that CREATE SECRET statements are stored in CLI
  history
- Recommend using environment variables or secret files in production
- Audited code for secret logging (found none - auth output only logs URLs and
  codes, not tokens)

**Impact:** Users are warned about the security implications of using secrets in
interactive SQL.

**Files Modified:**

- `README.md`

### 8. Documented Global State Lifecycle

**Changes:**

- Added detailed comment explaining `g_cfs` global pointer lifecycle
- Documented that `CloudSecretRegistry::Clear()` prevents stale pointers
- Explained dispatch table clearing prevents use-after-free on reloads
- Noted that DuckDB API lacks per-instance storage or teardown hooks

**Impact:** While not fully eliminating global state (limited by DuckDB
extension API), the design is now documented and safe through explicit clearing.

**Files Modified:**

- `src/extension/cloudfs_extension.cpp`
- `src/core/cloud_secret.cpp`

## 🔄 Recommended Future Improvements

### 5. Replace Hand-Rolled JSON Helpers

**Recommendation:** Replace `JsonUtil` with a proper JSON parser like
nlohmann/json or simdjson.

**Rationale:** The current hand-rolled parser is brittle and will fail on:

- Nested structures
- Escaped edge cases
- Malformed-but-common provider responses
- Unicode escape sequences beyond basic ASCII

**Required Changes:**

- Add nlohmann/json or simdjson to `vcpkg.json` dependencies
- Update CMakeLists.txt to link the JSON library
- Replace `JsonUtil::GetString/GetArray/GetInt64/GetBool` with proper JSON
  parsing
- Keep `JsonUtil::MakeObject` for simple JSON building or use JSON library's
  builder

**Risk:** Low risk of breaking changes if done carefully with tests. The
existing API surface is small.

**Estimated Effort:** 4-6 hours (add dependency, update all call sites, test).

## Summary

7 out of 8 high-priority security issues have been addressed. The remaining
issue (replacing JSON parser) requires adding a dependency but is recommended
for production use with complex cloud provider APIs.

All changes maintain backward compatibility and have been implemented with
minimal disruption to existing code.
