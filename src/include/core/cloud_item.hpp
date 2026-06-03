#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// CloudItem
// Provider-agnostic representation of a file or folder in any cloud storage.
// Each provider's backend translates its native API objects into this struct.
// ─────────────────────────────────────────────────────────────────────────────
struct CloudItem {
    std::string id;            // opaque provider ID (Graph item ID, Drive file ID, …)
    std::string name;          // filename or folder name
    std::string path;          // full path within the "drive" (provider-relative)
    std::string parent_id;     // parent folder ID
    std::string etag;          // cache validation token
    std::string download_url;  // pre-authenticated URL (may be empty; use ReadRange())
    std::string mime_type;
    int64_t     size              = 0;
    int64_t     modified_time_ms  = 0;   // Unix milliseconds
    bool        is_folder         = false;

    bool IsValid()  const { return !id.empty(); }
    bool IsFile()   const { return !is_folder; }
    bool IsFolder() const { return  is_folder; }
};

// ─────────────────────────────────────────────────────────────────────────────
// CloudUploadSession
// Encapsulates the state for a resumable/chunked upload.
// The ICloudBackend implementation fills this during CreateUploadSession().
// ─────────────────────────────────────────────────────────────────────────────
struct CloudUploadSession {
    std::string upload_url;         // where to PUT chunks
    std::string item_id;            // (optional) item ID if known before completion
    int64_t     chunk_size_bytes;   // required alignment (e.g. 320 KiB for SharePoint)
    int64_t     total_size_bytes  = -1; // -1 = unknown until finalize
    bool        requires_total_upfront = false; // Dropbox needs total size at start
};

} // namespace duckdb
