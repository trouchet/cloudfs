#include "core/cloud_table_functions.hpp"
#include "core/cloud_filesystem.hpp"
#include "core/cloud_backend.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <fnmatch.h>
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace duckdb {

static CloudFileSystem *g_tf_cfs = nullptr;

void SetCloudFS(CloudFileSystem *cfs) { g_tf_cfs = cfs; }

static CloudFileSystem *GetCFS(optional_ptr<TableFunctionInfo>) { return g_tf_cfs; }

// ─── Schema shared by ls() and stat() ────────────────────────────────────────
static void AddLsColumns(vector<LogicalType> &types, vector<string> &names) {
    names.emplace_back("url");        types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("name");       types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("type");       types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("size");       types.emplace_back(LogicalType::BIGINT);
    names.emplace_back("size_pretty");types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("modified");   types.emplace_back(LogicalType::TIMESTAMP);
    names.emplace_back("etag");       types.emplace_back(LogicalType::VARCHAR);
}

static std::string PrettySizeImpl(int64_t bytes) {
    if (bytes < 0)              return "";
    if (bytes < 1024)           return std::to_string(bytes) + " B";
    if (bytes < 1024*1024)      return std::to_string(bytes/1024) + " KiB";
    if (bytes < 1024*1024*1024) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f MiB", (double)bytes/(1024.0*1024));
        return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f GiB", (double)bytes/(1024.0*1024*1024));
    return buf;
}

// ─── ls() ─────────────────────────────────────────────────────────────────────

struct LsBindData : public FunctionData {
    CloudFileSystem  *cfs;
    std::string       url;         // base URL to list
    bool              recursive;
    std::string       pattern;     // fnmatch pattern for filtering
    std::vector<std::pair<std::string,CloudItem>> rows; // pre-fetched during bind

    LsBindData(CloudFileSystem *cfs_, std::string url_,
               bool rec, std::string pat)
        : cfs(cfs_), url(std::move(url_)), recursive(rec), pattern(std::move(pat)) {}

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<LsBindData>(cfs, url, recursive, pattern);
        copy->rows = rows;
        return std::move(copy);
    }
    bool Equals(const FunctionData &other) const override {
        auto &o = other.Cast<LsBindData>();
        return url == o.url && recursive == o.recursive && pattern == o.pattern;
    }
};

struct LsScanState : public GlobalTableFunctionState {
    idx_t offset = 0;
};

// Walk a folder recursively and collect (full_url, CloudItem) pairs
static void WalkFolder(ICloudBackend &backend,
                       const std::string &root_id,
                       const std::string &folder_id,
                       const std::string &url_prefix,
                       bool recursive,
                       const std::string &pattern,
                       const std::string &token,
                       std::vector<std::pair<std::string,CloudItem>> &out) {
    std::string cursor, err;
    do {
        std::vector<CloudItem> batch;
        if (!backend.ListFolder(root_id, folder_id, token,
                                [&](const CloudItem &c){ batch.push_back(c); },
                                cursor, err)) break;
        for (auto &item : batch) {
            std::string item_url = url_prefix + "/" + item.name;
            bool name_matches = (pattern.empty() || pattern == "*" ||
                                 fnmatch(pattern.c_str(), item.name.c_str(), 0) == 0);
            if (name_matches || item.is_folder)
                out.emplace_back(item_url, item);
            if (item.is_folder && recursive)
                WalkFolder(backend, root_id, item.id, item_url,
                           recursive, pattern, token, out);
        }
    } while (!cursor.empty());
}

static unique_ptr<FunctionData> LsBind(ClientContext &ctx,
                                        TableFunctionBindInput &input,
                                        vector<LogicalType> &ret_types,
                                        vector<string> &names) {
    AddLsColumns(ret_types, names);

    auto *cfs = GetCFS(input.info);
    std::string url = input.inputs[0].ToString();
    bool recursive  = false;
    std::string pat = "*";

    for (auto &kv : input.named_parameters) {
        if (kv.first == "recursive") recursive = kv.second.GetValue<bool>();
        if (kv.first == "pattern")   pat = kv.second.ToString();
    }

    auto data = make_uniq<LsBindData>(cfs, url, recursive, pat);

    // Pre-fetch all items during bind (small result sets typical for ls)
    auto *b = cfs->BackendForPublic(url);
    if (!b) return std::move(data);

    std::string raw_root, item_path, err, root_id, tok;
    if (!b->ParseUrl(url, raw_root, item_path, err)) return std::move(data);
    if (!cfs->GetRootIdPublic(b, raw_root, tok, root_id)) return std::move(data);
    if (tok.empty()) cfs->GetTokenPublic(b->Scheme(), tok, err);

    // Stat the target to determine if it's a file or folder
    CloudItem target;
    if (!b->Stat(root_id, item_path, tok, target, err)) return std::move(data);

    if (!target.is_folder) {
        // Single file — return it directly
        data->rows.emplace_back(url, target);
    } else {
        // Directory — list children
        std::string url_prefix = url;
        while (!url_prefix.empty() && url_prefix.back() == '/')
            url_prefix.pop_back();
        WalkFolder(*b, root_id, target.id, url_prefix,
                   recursive, pat, tok, data->rows);
    }
    return std::move(data);
}

static unique_ptr<GlobalTableFunctionState> LsInit(ClientContext &,
                                                     TableFunctionInitInput &) {
    return make_uniq<LsScanState>();
}

static void LsScan(ClientContext &, TableFunctionInput &data_p, DataChunk &output) {
    auto &data  = data_p.bind_data->Cast<LsBindData>();
    auto &state = data_p.global_state->Cast<LsScanState>();

    idx_t count = 0;
    while (state.offset < data.rows.size() && count < STANDARD_VECTOR_SIZE) {
        auto &[item_url, item] = data.rows[state.offset];

        output.data[0].SetValue(count, Value(item_url));
        output.data[1].SetValue(count, Value(item.name));
        output.data[2].SetValue(count, Value(item.is_folder ? "directory" : "file"));

        if (item.is_folder) {
            output.data[3].SetValue(count, Value());          // size NULL
            output.data[4].SetValue(count, Value());          // size_pretty NULL
        } else {
            output.data[3].SetValue(count, Value::BIGINT(item.size));
            output.data[4].SetValue(count, Value(PrettySizeImpl(item.size)));
        }

        if (item.modified_time_ms > 0) {
            auto ts = Timestamp::FromEpochMs(item.modified_time_ms);
            output.data[5].SetValue(count, Value::TIMESTAMP(ts));
        } else {
            output.data[5].SetValue(count, Value());
        }

        output.data[6].SetValue(count, Value(item.etag));

        ++count;
        ++state.offset;
    }
    output.SetCardinality(count);
}

TableFunction LsTableFunction(CloudFileSystem *cfs) {
    TableFunction tf("ls", {LogicalType::VARCHAR}, LsScan, LsBind, LsInit);
    tf.named_parameters["recursive"] = LogicalType::BOOLEAN;
    tf.named_parameters["pattern"]   = LogicalType::VARCHAR;
    return tf;
}

// ─── stat() ───────────────────────────────────────────────────────────────────

struct StatBindData : public FunctionData {
    CloudFileSystem *cfs;
    std::string      url;
    CloudItem        item;
    std::string      item_url;
    bool             found = false;

    StatBindData(CloudFileSystem *cfs_, std::string url_)
        : cfs(cfs_), url(std::move(url_)) {}
    unique_ptr<FunctionData> Copy() const override {
        auto c = make_uniq<StatBindData>(cfs, url);
        c->item = item; c->item_url = item_url; c->found = found;
        return std::move(c);
    }
    bool Equals(const FunctionData &other) const override {
        return url == other.Cast<StatBindData>().url;
    }
};

struct StatScanState : public GlobalTableFunctionState {
    bool emitted = false;
};

static unique_ptr<FunctionData> StatBind(ClientContext &,
                                          TableFunctionBindInput &input,
                                          vector<LogicalType> &ret_types,
                                          vector<string> &names) {
    AddLsColumns(ret_types, names);

    auto *cfs = GetCFS(input.info);
    std::string url = input.inputs[0].ToString();
    auto data = make_uniq<StatBindData>(cfs, url);

    auto *b = cfs->BackendForPublic(url);
    if (!b) return std::move(data);

    std::string raw_root, item_path, err, root_id, tok;
    if (!b->ParseUrl(url, raw_root, item_path, err)) return std::move(data);
    if (!cfs->GetRootIdPublic(b, raw_root, tok, root_id)) return std::move(data);
    if (tok.empty()) cfs->GetTokenPublic(b->Scheme(), tok, err);

    if (b->Stat(root_id, item_path, tok, data->item, err)) {
        data->item_url = url;
        data->found    = true;
    }
    return std::move(data);
}

static unique_ptr<GlobalTableFunctionState> StatInit(ClientContext &,
                                                       TableFunctionInitInput &) {
    return make_uniq<StatScanState>();
}

static void StatScan(ClientContext &, TableFunctionInput &data_p, DataChunk &output) {
    auto &data  = data_p.bind_data->Cast<StatBindData>();
    auto &state = data_p.global_state->Cast<StatScanState>();
    if (state.emitted || !data.found) { output.SetCardinality(0); return; }
    state.emitted = true;

    auto &item = data.item;
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
        output.data[5].SetValue(0, Value::TIMESTAMP(
            Timestamp::FromEpochMs(item.modified_time_ms)));
    } else {
        output.data[5].SetValue(0, Value());
    }
    output.data[6].SetValue(0, Value(item.etag));
    output.SetCardinality(1);
}

TableFunction StatTableFunction(CloudFileSystem *cfs) {
    TableFunction tf("stat", {LogicalType::VARCHAR}, StatScan, StatBind, StatInit);
    return tf;
}

// ─── du() ─────────────────────────────────────────────────────────────────────

struct DuRow {
    std::string directory;
    int64_t     file_count  = 0;
    int64_t     total_size  = 0;
};

struct DuBindData : public FunctionData {
    CloudFileSystem *cfs;
    std::string      url;
    std::vector<DuRow> rows;

    DuBindData(CloudFileSystem *cfs_, std::string url_)
        : cfs(cfs_), url(std::move(url_)) {}
    unique_ptr<FunctionData> Copy() const override {
        auto c = make_uniq<DuBindData>(cfs, url);
        c->rows = rows;
        return std::move(c);
    }
    bool Equals(const FunctionData &other) const override {
        return url == other.Cast<DuBindData>().url;
    }
};

struct DuScanState : public GlobalTableFunctionState { idx_t offset = 0; };

// Walk and accumulate size per directory
static void WalkForDu(ICloudBackend &backend,
                       const std::string &root_id,
                       const std::string &folder_id,
                       const std::string &folder_url,
                       const std::string &token,
                       std::vector<DuRow> &rows) {
    DuRow row;
    row.directory = folder_url;

    std::string cursor, err;
    std::vector<CloudItem> subdirs;
    do {
        std::vector<CloudItem> batch;
        if (!backend.ListFolder(root_id, folder_id, token,
                                [&](const CloudItem &c){ batch.push_back(c); },
                                cursor, err)) break;
        for (auto &item : batch) {
            if (item.is_folder) {
                subdirs.emplace_back(item);
            } else {
                row.file_count++;
                row.total_size += item.size;
            }
        }
    } while (!cursor.empty());

    rows.push_back(row);

    for (auto &sub : subdirs) {
        std::string sub_url = folder_url + "/" + sub.name;
        WalkForDu(backend, root_id, sub.id, sub_url, token, rows);
    }
}

static unique_ptr<FunctionData> DuBind(ClientContext &,
                                        TableFunctionBindInput &input,
                                        vector<LogicalType> &ret_types,
                                        vector<string> &names) {
    names.emplace_back("directory");   ret_types.emplace_back(LogicalType::VARCHAR);
    names.emplace_back("file_count");  ret_types.emplace_back(LogicalType::BIGINT);
    names.emplace_back("total_size");  ret_types.emplace_back(LogicalType::BIGINT);
    names.emplace_back("size_pretty"); ret_types.emplace_back(LogicalType::VARCHAR);

    auto *cfs = GetCFS(input.info);
    std::string url = input.inputs[0].ToString();
    auto data = make_uniq<DuBindData>(cfs, url);

    auto *b = cfs->BackendForPublic(url);
    if (!b) return std::move(data);

    std::string raw_root, item_path, err, root_id, tok;
    if (!b->ParseUrl(url, raw_root, item_path, err)) return std::move(data);
    if (!cfs->GetRootIdPublic(b, raw_root, tok, root_id)) return std::move(data);
    if (tok.empty()) cfs->GetTokenPublic(b->Scheme(), tok, err);

    CloudItem folder;
    if (!b->Stat(root_id, item_path, tok, folder, err)) return std::move(data);

    std::string url_clean = url;
    while (!url_clean.empty() && url_clean.back() == '/') url_clean.pop_back();

    WalkForDu(*b, root_id, folder.id, url_clean, tok, data->rows);
    return std::move(data);
}

static unique_ptr<GlobalTableFunctionState> DuInit(ClientContext &,
                                                     TableFunctionInitInput &) {
    return make_uniq<DuScanState>();
}

static void DuScan(ClientContext &, TableFunctionInput &data_p, DataChunk &output) {
    auto &data  = data_p.bind_data->Cast<DuBindData>();
    auto &state = data_p.global_state->Cast<DuScanState>();

    idx_t count = 0;
    while (state.offset < data.rows.size() && count < STANDARD_VECTOR_SIZE) {
        auto &row = data.rows[state.offset];
        output.data[0].SetValue(count, Value(row.directory));
        output.data[1].SetValue(count, Value::BIGINT(row.file_count));
        output.data[2].SetValue(count, Value::BIGINT(row.total_size));
        output.data[3].SetValue(count, Value(PrettySizeImpl(row.total_size)));
        ++count; ++state.offset;
    }
    output.SetCardinality(count);
}

TableFunction DuTableFunction(CloudFileSystem *cfs) {
    TableFunction tf("du", {LogicalType::VARCHAR}, DuScan, DuBind, DuInit);
    return tf;
}

// ─── Registration ─────────────────────────────────────────────────────────────
void RegisterCloudTableFunctions(ExtensionLoader &loader) {
    loader.RegisterFunction(LsTableFunction(g_tf_cfs));
    loader.RegisterFunction(StatTableFunction(g_tf_cfs));
    loader.RegisterFunction(DuTableFunction(g_tf_cfs));
}

} // namespace duckdb
