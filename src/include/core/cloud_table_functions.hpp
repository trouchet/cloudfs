#pragma once
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "cloud_filesystem.hpp"

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// ls(url, recursive := false, pattern := '*')
//
// Lists files and directories at a cloud URL with full metadata.
// Works with all registered backends (spfs://, odfs://, gdfs://, dbxfs://, vfs://).
//
// Returns:
//   url         VARCHAR   -- full qualified URL of the item
//   name        VARCHAR   -- filename or folder name
//   type        VARCHAR   -- 'file' | 'directory'
//   size        BIGINT    -- bytes (NULL for directories)
//   size_pretty VARCHAR   -- human-readable: '1.2 MiB', '430 KiB'
//   modified    TIMESTAMP -- last modified (NULL if unknown)
//   etag        VARCHAR   -- cache validation token
//
// Usage:
//   SELECT * FROM ls('spfs://contoso.sharepoint.com/sites/X/Docs/');
//   SELECT * FROM ls('gdfs://MyDrive/analytics/', recursive := true);
//   SELECT * FROM ls('vfs://10.0.0.5:8765/data/', pattern := '*.parquet');
//   SELECT sum(size) FROM ls('dbxfs:///reports/', recursive := true)
//            WHERE type = 'file' AND name LIKE '%.parquet';
// ─────────────────────────────────────────────────────────────────────────────
TableFunction LsTableFunction(CloudFileSystem* cfs);

// ─────────────────────────────────────────────────────────────────────────────
// stat(url)
//
// Returns metadata for a single file or directory.
// Same columns as ls() but exactly one row.
//
// Usage:
//   SELECT size_pretty, modified FROM stat('spfs://tenant.sharepoint.com/.../file.parquet');
// ─────────────────────────────────────────────────────────────────────────────
TableFunction StatTableFunction(CloudFileSystem* cfs);

// ─────────────────────────────────────────────────────────────────────────────
// du(url)
//
// Disk-usage summary — recursive size rollup by directory.
// Returns one row per directory with total size of all files within it.
//
// Columns: directory VARCHAR, file_count BIGINT, total_size BIGINT, size_pretty VARCHAR
//
// Usage:
//   SELECT * FROM du('spfs://contoso.sharepoint.com/sites/X/Docs/')
//   ORDER BY total_size DESC LIMIT 10;
// ─────────────────────────────────────────────────────────────────────────────
TableFunction DuTableFunction(CloudFileSystem* cfs);

// ─────────────────────────────────────────────────────────────────────────────
// Helper: register all three with the loader
// ───────────────────────────────────────────────────────────────────────────
void RegisterCloudTableFunctions(ExtensionLoader& loader, CloudFileSystem* cfs);

} // namespace duckdb
