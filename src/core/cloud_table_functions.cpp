#include "core/cloud_table_functions.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/function/table_function.hpp"

#include "core/cloud_backend.hpp"
#include "core/cloud_filesystem.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

// Portable glob wildcard match (replaces POSIX fnmatch for Windows compat).
// Supports '*' (any sequence) and '?' (any single char). Returns 0 on match.
static int glob_match(const char* pattern, const char* name) {
    while (*pattern && *name) {
        if (*pattern == '*') {
            while (*pattern == '*')
                ++pattern;
            if (!*pattern)
                return 0;
            while (*name)
                if (glob_match(pattern, name++) == 0)
                    return 0;
            return 1;
        }
        if (*pattern != '?' && *pattern != *name)
            return 1;
        ++pattern;
        ++name;
    }
    while (*pattern == '*')
        ++pattern;
    return (*pattern || *name) ? 1 : 0;
}

namespace duckdb {

// ─── CloudFSInfo: TableFunctionInfo holding CloudFileSystem pointer ──────────
struct CloudFSInfo : public TableFunctionInfo {
    CloudFileSystem* cfs;
    explicit CloudFSInfo(CloudFileSystem* cfs_) : cfs(cfs_) {}
};

static CloudFileSystem* GetCFS(optional_ptr<TableFunctionInfo> info) {
    if (!info) {
        throw InternalException("CloudFS table function called without TableFunctionInfo");
    }
    auto* cfs_info = dynamic_cast<CloudFSInfo*>(info.get());
    if (!cfs_info) {
        throw InternalException("TableFunctionInfo is not CloudFSInfo");
    }
    return cfs_info->cfs;
}

// ─── Schema shared by ls() and stat() ────────────────────────────────────────
static void AddLsColumns(vector<LogicalType>& types, vector<string>& names) {
    names.emplace_back("url");
    types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("name");
    types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("type");
    types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("size");
    types.emplace_back(LogicalType::BIGINT);
    names.emplace_back("size_pretty");
    types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("modified");
    types.emplace_back(LogicalType::TIMESTAMP);
    names.emplace_back("etag");
    types.emplace_back(LogicalType::VARCHAR);
}

static std::string PrettySizeImpl(int64_t bytes) {
    if (bytes < 0)
        return "";
    if (bytes < 1024)
        return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024)
        return std::to_string(bytes / 1024) + " KiB";
    if (bytes < 1024 * 1024 * 1024) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f MiB", (double)bytes / (1024.0 * 1024));
        return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f GiB", (double)bytes / (1024.0 * 1024 * 1024));
    return buf;
}

// ─── ls() ─────────────────────────────────────────────────────────────────────

struct LsBindData : public FunctionData {
    ICloudBackend* backend;
    std::string root_id;
    std::string token;
    std::string url_prefix; // base URL without trailing slash
    bool recursive;
    std::string pattern; // fnmatch pattern
    CloudItem target;    // the initial file or folder to list
    bool is_single_file; // true if target is a file (return 1 row)

    LsBindData(ICloudBackend* b, std::string rid, std::string tok, std::string url_pre, bool rec,
               std::string pat, CloudItem tgt, bool single)
        : backend(b), root_id(std::move(rid)), token(std::move(tok)),
          url_prefix(std::move(url_pre)), recursive(rec), pattern(std::move(pat)),
          target(std::move(tgt)), is_single_file(single) {}

    unique_ptr<FunctionData> Copy() const override {
        return make_uniq<LsBindData>(backend, root_id, token, url_prefix, recursive, pattern,
                                     target, is_single_file);
    }
    bool Equals(const FunctionData& other) const override {
        auto& o = other.Cast<LsBindData>();
        return root_id == o.root_id && url_prefix == o.url_prefix && recursive == o.recursive &&
               pattern == o.pattern;
    }
};

struct LsScanState : public GlobalTableFunctionState {
    // Stack of (folder_id, url_prefix) pairs to visit
    std::vector<std::pair<std::string, std::string>> folder_stack;
    std::string current_cursor;                            // pagination cursor for current folder
    std::vector<std::pair<std::string, CloudItem>> buffer; // pending items to emit
    idx_t buffer_offset = 0;
    bool finished = false;
};

// Fetch one page of items from the current folder and populate buffer/stack
static void FetchNextBatch(const LsBindData& bind, LsScanState& state) {
    if (state.folder_stack.empty() && state.current_cursor.empty()) {
        state.finished = true;
        return;
    }

    // If no cursor, pop next folder from stack
    if (state.current_cursor.empty()) {
        if (state.folder_stack.empty()) {
            state.finished = true;
            return;
        }
        // Continue with current folder (cursor will be set by ListFolder)
    }

    auto [folder_id, url_prefix] = state.folder_stack.back();
    std::string err;
    std::vector<CloudItem> batch;

    if (!bind.backend->ListFolder(
            bind.root_id, folder_id, bind.token, [&](const CloudItem& c) { batch.push_back(c); },
            state.current_cursor, err)) {
        // Error during listing - mark as finished to avoid infinite loop
        state.finished = true;
        return;
    }

    // Process batch: add matching items to buffer, push folders to stack
    std::vector<std::pair<std::string, std::string>> new_folders;
    for (auto& item : batch) {
        std::string item_url = url_prefix + "/" + item.name;
        bool name_matches = (bind.pattern.empty() || bind.pattern == "*" ||
                             glob_match(bind.pattern.c_str(), item.name.c_str()) == 0);

        if (name_matches || item.is_folder) {
            state.buffer.emplace_back(item_url, item);
        }

        if (item.is_folder && bind.recursive) {
            new_folders.emplace_back(item.id, item_url);
        }
    }

    // If cursor is empty, we finished this folder - pop it and push children
    if (state.current_cursor.empty()) {
        state.folder_stack.pop_back();
        // Push new folders in reverse order (depth-first traversal)
        for (auto it = new_folders.rbegin(); it != new_folders.rend(); ++it) {
            state.folder_stack.push_back(*it);
        }
    }
}

static unique_ptr<FunctionData> LsBind(ClientContext& ctx, TableFunctionBindInput& input,
                                       vector<LogicalType>& ret_types, vector<string>& names) {
    AddLsColumns(ret_types, names);

    auto* cfs = GetCFS(input.info);
    std::string url = input.inputs[0].ToString();
    bool recursive = false;
    std::string pat = "*";

    for (auto& kv : input.named_parameters) {
        if (kv.first == "recursive")
            recursive = kv.second.GetValue<bool>();
        if (kv.first == "pattern")
            pat = kv.second.ToString();
    }

    // Validate URL and prepare parameters (no I/O walk)
    auto* b = cfs->BackendForPublic(url);
    if (!b)
        throw InvalidInputException("No backend registered for URL: %s", url);

    std::string raw_root, item_path, err, root_id, tok;
    if (!b->ParseUrl(url, raw_root, item_path, err))
        throw InvalidInputException("Failed to parse URL '%s': %s", url, err);
    if (!cfs->GetRootIdPublic(b, raw_root, tok, root_id))
        throw InvalidInputException("Failed to resolve root ID for URL '%s'. Check credentials.",
                                    url);
    if (tok.empty())
        cfs->GetTokenPublic(b->Scheme(), tok, err);

    // Stat the target to determine if it's a file or folder
    CloudItem target;
    if (!b->Stat(root_id, item_path, tok, target, err))
        throw InvalidInputException("Failed to stat '%s': %s", url, err);

    std::string url_prefix = url;
    while (!url_prefix.empty() && url_prefix.back() == '/')
        url_prefix.pop_back();

    bool is_single_file = !target.is_folder;
    return make_uniq<LsBindData>(b, root_id, tok, url_prefix, recursive, pat, target,
                                 is_single_file);
}

static unique_ptr<GlobalTableFunctionState> LsInit(ClientContext&, TableFunctionInitInput& input) {
    auto& bind = input.bind_data->Cast<LsBindData>();
    auto state = make_uniq<LsScanState>();

    if (bind.is_single_file) {
        // Single file: add it to buffer directly
        state->buffer.emplace_back(bind.url_prefix, bind.target);
        state->finished = true;
    } else {
        // Directory: push to stack to start iteration
        state->folder_stack.emplace_back(bind.target.id, bind.url_prefix);
    }

    return std::move(state);
}

static void LsScan(ClientContext& context, TableFunctionInput& data_p, DataChunk& output) {
    auto& bind = data_p.bind_data->Cast<LsBindData>();
    auto& state = data_p.global_state->Cast<LsScanState>();

    idx_t count = 0;

    // Emit buffered items first
    while (state.buffer_offset < state.buffer.size() && count < STANDARD_VECTOR_SIZE) {
        auto& [item_url, item] = state.buffer[state.buffer_offset];

        output.data[0].SetValue(count, Value(item_url));
        output.data[1].SetValue(count, Value(item.name));
        output.data[2].SetValue(count, Value(item.is_folder ? "directory" : "file"));

        if (item.is_folder) {
            output.data[3].SetValue(count, Value());
            output.data[4].SetValue(count, Value());
        } else {
            output.data[3].SetValue(count, Value::BIGINT(item.size));
            output.data[4].SetValue(count, Value(PrettySizeImpl(item.size)));
        }

        if (item.modified_time_ms > 0) {
            output.data[5].SetValue(
                count, Value::TIMESTAMP(Timestamp::FromEpochMs(item.modified_time_ms)));
        } else {
            output.data[5].SetValue(count, Value());
        }

        output.data[6].SetValue(count, Value(item.etag));

        ++count;
        ++state.buffer_offset;
    }

    // If buffer exhausted, fetch next batch
    if (state.buffer_offset >= state.buffer.size() && !state.finished) {
        state.buffer.clear();
        state.buffer_offset = 0;
        FetchNextBatch(bind, state);

        // Recursively call to fill output (tail recursion optimized away by compiler)
        if (!state.buffer.empty() && count < STANDARD_VECTOR_SIZE) {
            return LsScan(context, data_p, output);
        }
    }

    output.SetCardinality(count);
}

TableFunction LsTableFunction(CloudFileSystem* cfs) {
    TableFunction tf("ls", {LogicalType::VARCHAR}, LsScan, LsBind, LsInit);
    tf.named_parameters["recursive"] = LogicalType::BOOLEAN;
    tf.named_parameters["pattern"] = LogicalType::VARCHAR;
    tf.function_info = make_shared_ptr<CloudFSInfo>(cfs);
    return tf;
}

// ─── stat() ───────────────────────────────────────────────────────────────────

struct StatBindData : public FunctionData {
    CloudFileSystem* cfs;
    std::string url;
    CloudItem item;
    std::string item_url;
    bool found = false;

    StatBindData(CloudFileSystem* cfs_, std::string url_) : cfs(cfs_), url(std::move(url_)) {}
    unique_ptr<FunctionData> Copy() const override {
        auto c = make_uniq<StatBindData>(cfs, url);
        c->item = item;
        c->item_url = item_url;
        c->found = found;
        return std::move(c);
    }
    bool Equals(const FunctionData& other) const override {
        return url == other.Cast<StatBindData>().url;
    }
};

struct StatScanState : public GlobalTableFunctionState {
    bool emitted = false;
};

static unique_ptr<FunctionData> StatBind(ClientContext&, TableFunctionBindInput& input,
                                         vector<LogicalType>& ret_types, vector<string>& names) {
    AddLsColumns(ret_types, names);

    auto* cfs = GetCFS(input.info);
    std::string url = input.inputs[0].ToString();
    auto data = make_uniq<StatBindData>(cfs, url);

    auto* b = cfs->BackendForPublic(url);
    if (!b)
        throw InvalidInputException("No backend registered for URL: %s", url);

    std::string raw_root, item_path, err, root_id, tok;
    if (!b->ParseUrl(url, raw_root, item_path, err))
        throw InvalidInputException("Failed to parse URL '%s': %s", url, err);
    if (!cfs->GetRootIdPublic(b, raw_root, tok, root_id))
        throw InvalidInputException("Failed to resolve root ID for URL '%s'. Check credentials.",
                                    url);
    if (tok.empty())
        cfs->GetTokenPublic(b->Scheme(), tok, err);

    if (b->Stat(root_id, item_path, tok, data->item, err)) {
        data->item_url = url;
        data->found = true;
    } else {
        throw InvalidInputException("Failed to stat '%s': %s", url, err);
    }
    return std::move(data);
}

static unique_ptr<GlobalTableFunctionState> StatInit(ClientContext&, TableFunctionInitInput&) {
    return make_uniq<StatScanState>();
}

static void StatScan(ClientContext&, TableFunctionInput& data_p, DataChunk& output) {
    auto& data = data_p.bind_data->Cast<StatBindData>();
    auto& state = data_p.global_state->Cast<StatScanState>();
    if (state.emitted || !data.found) {
        output.SetCardinality(0);
        return;
    }
    state.emitted = true;

    auto& item = data.item;
    output.data[0].SetValue(0, Value(data.item_url));
    output.data[1].SetValue(0, Value(item.name));
    output.data[2].SetValue(0, Value(item.is_folder ? "directory" : "file"));
    if (item.is_folder) {
        output.data[3].SetValue(0, Value());
        output.data[4].SetValue(0, Value());
    } else {
        output.data[3].SetValue(0, Value::BIGINT(item.size));
        output.data[4].SetValue(0, Value(PrettySizeImpl(item.size)));
    }
    if (item.modified_time_ms > 0) {
        output.data[5].SetValue(0, Value::TIMESTAMP(Timestamp::FromEpochMs(item.modified_time_ms)));
    } else {
        output.data[5].SetValue(0, Value());
    }
    output.data[6].SetValue(0, Value(item.etag));
    output.SetCardinality(1);
}

TableFunction StatTableFunction(CloudFileSystem* cfs) {
    TableFunction tf("stat", {LogicalType::VARCHAR}, StatScan, StatBind, StatInit);
    tf.function_info = make_shared_ptr<CloudFSInfo>(cfs);
    return tf;
}

// ─── du() ─────────────────────────────────────────────────────────────────────

struct DuRow {
    std::string directory;
    int64_t file_count = 0;
    int64_t total_size = 0;
};

struct DuBindData : public FunctionData {
    ICloudBackend* backend;
    std::string root_id;
    std::string token;
    std::string url_prefix;
    CloudItem target; // root folder to scan

    DuBindData(ICloudBackend* b, std::string rid, std::string tok, std::string url_pre,
               CloudItem tgt)
        : backend(b), root_id(std::move(rid)), token(std::move(tok)),
          url_prefix(std::move(url_pre)), target(std::move(tgt)) {}

    unique_ptr<FunctionData> Copy() const override {
        return make_uniq<DuBindData>(backend, root_id, token, url_prefix, target);
    }
    bool Equals(const FunctionData& other) const override {
        auto& o = other.Cast<DuBindData>();
        return root_id == o.root_id && url_prefix == o.url_prefix;
    }
};

struct DuScanState : public GlobalTableFunctionState {
    // Stack: (folder_id, folder_url, subfolder_ids_to_visit)
    struct FolderFrame {
        std::string folder_id;
        std::string folder_url;
        DuRow accumulated;                                        // counts for this folder
        std::vector<std::pair<std::string, std::string>> subdirs; // (id, url) pairs
        idx_t subdir_index = 0;
        std::string cursor;  // pagination cursor
        bool listed = false; // true when ListFolder completed
    };

    std::vector<FolderFrame> stack;
    std::vector<DuRow> results_buffer; // completed folders ready to emit
    idx_t results_offset = 0;
    bool finished = false;
};

// Process next folder in the du() scan
static void ProcessDuFolder(const DuBindData& bind, DuScanState& state) {
    if (state.stack.empty()) {
        state.finished = true;
        return;
    }

    auto& frame = state.stack.back();

    // If not yet listed, fetch items from this folder
    if (!frame.listed) {
        std::string err;
        std::vector<CloudItem> batch;

        if (!bind.backend->ListFolder(
                bind.root_id, frame.folder_id, bind.token,
                [&](const CloudItem& c) { batch.push_back(c); }, frame.cursor, err)) {
            // Error - mark as listed to skip
            frame.listed = true;
            frame.cursor.clear();
            return;
        }

        // Accumulate files and collect subdirs
        for (auto& item : batch) {
            if (item.is_folder) {
                std::string subdir_url = frame.folder_url + "/" + item.name;
                frame.subdirs.emplace_back(item.id, subdir_url);
            } else {
                frame.accumulated.file_count++;
                frame.accumulated.total_size += item.size;
            }
        }

        // If pagination done, mark as listed
        if (frame.cursor.empty()) {
            frame.listed = true;
        }
        return; // Continue pagination or move to subdirs
    }

    // Folder fully listed - process subdirs
    if (frame.subdir_index < frame.subdirs.size()) {
        auto [subdir_id, subdir_url] = frame.subdirs[frame.subdir_index];
        frame.subdir_index++;

        // Push subdir to stack
        DuScanState::FolderFrame child;
        child.folder_id = subdir_id;
        child.folder_url = subdir_url;
        child.accumulated.directory = subdir_url;
        state.stack.push_back(child);
        return;
    }

    // All subdirs processed - emit this folder and pop
    frame.accumulated.directory = frame.folder_url;
    state.results_buffer.push_back(frame.accumulated);
    state.stack.pop_back();
}

static unique_ptr<FunctionData> DuBind(ClientContext&, TableFunctionBindInput& input,
                                       vector<LogicalType>& ret_types, vector<string>& names) {
    names.emplace_back("directory");
    ret_types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("file_count");
    ret_types.emplace_back(LogicalType::BIGINT);
    names.emplace_back("total_size");
    ret_types.emplace_back(LogicalType::BIGINT);
    names.emplace_back("size_pretty");
    ret_types.emplace_back(LogicalType::VARCHAR);

    auto* cfs = GetCFS(input.info);
    std::string url = input.inputs[0].ToString();

    // Validate URL and prepare parameters (no I/O walk)
    auto* b = cfs->BackendForPublic(url);
    if (!b)
        throw InvalidInputException("No backend registered for URL: %s", url);

    std::string raw_root, item_path, err, root_id, tok;
    if (!b->ParseUrl(url, raw_root, item_path, err))
        throw InvalidInputException("Failed to parse URL '%s': %s", url, err);
    if (!cfs->GetRootIdPublic(b, raw_root, tok, root_id))
        throw InvalidInputException("Failed to resolve root ID for URL '%s'. Check credentials.",
                                    url);
    if (tok.empty())
        cfs->GetTokenPublic(b->Scheme(), tok, err);

    CloudItem folder;
    if (!b->Stat(root_id, item_path, tok, folder, err))
        throw InvalidInputException("Failed to stat '%s': %s", url, err);

    std::string url_clean = url;
    while (!url_clean.empty() && url_clean.back() == '/')
        url_clean.pop_back();

    return make_uniq<DuBindData>(b, root_id, tok, url_clean, folder);
}

static unique_ptr<GlobalTableFunctionState> DuInit(ClientContext&, TableFunctionInitInput& input) {
    auto& bind = input.bind_data->Cast<DuBindData>();
    auto state = make_uniq<DuScanState>();

    // Push root folder to stack
    DuScanState::FolderFrame root;
    root.folder_id = bind.target.id;
    root.folder_url = bind.url_prefix;
    root.accumulated.directory = bind.url_prefix;
    state->stack.push_back(root);

    return std::move(state);
}

static void DuScan(ClientContext&, TableFunctionInput& data_p, DataChunk& output) {
    auto& bind = data_p.bind_data->Cast<DuBindData>();
    auto& state = data_p.global_state->Cast<DuScanState>();

    idx_t count = 0;

    // Emit buffered results first
    while (state.results_offset < state.results_buffer.size() && count < STANDARD_VECTOR_SIZE) {
        auto& row = state.results_buffer[state.results_offset];
        output.data[0].SetValue(count, Value(row.directory));
        output.data[1].SetValue(count, Value::BIGINT(row.file_count));
        output.data[2].SetValue(count, Value::BIGINT(row.total_size));
        output.data[3].SetValue(count, Value(PrettySizeImpl(row.total_size)));
        ++count;
        ++state.results_offset;
    }

    // If buffer exhausted, process more folders
    while (count < STANDARD_VECTOR_SIZE && !state.finished) {
        if (state.results_offset >= state.results_buffer.size()) {
            state.results_buffer.clear();
            state.results_offset = 0;
        }

        ProcessDuFolder(bind, state);

        // Emit newly added results
        while (state.results_offset < state.results_buffer.size() && count < STANDARD_VECTOR_SIZE) {
            auto& row = state.results_buffer[state.results_offset];
            output.data[0].SetValue(count, Value(row.directory));
            output.data[1].SetValue(count, Value::BIGINT(row.file_count));
            output.data[2].SetValue(count, Value::BIGINT(row.total_size));
            output.data[3].SetValue(count, Value(PrettySizeImpl(row.total_size)));
            ++count;
            ++state.results_offset;
        }

        // Avoid infinite loop if no progress
        if (state.results_buffer.empty() && !state.finished) {
            break;
        }
    }

    output.SetCardinality(count);
}

TableFunction DuTableFunction(CloudFileSystem* cfs) {
    TableFunction tf("du", {LogicalType::VARCHAR}, DuScan, DuBind, DuInit);
    tf.function_info = make_shared_ptr<CloudFSInfo>(cfs);
    return tf;
}

// ─── Registration ─────────────────────────────────────────────────────────────
void RegisterCloudTableFunctions(ExtensionLoader& loader, CloudFileSystem* cfs) {
    loader.RegisterFunction(LsTableFunction(cfs));
    loader.RegisterFunction(StatTableFunction(cfs));
    loader.RegisterFunction(DuTableFunction(cfs));
}

} // namespace duckdb
