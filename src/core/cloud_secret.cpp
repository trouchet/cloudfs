#include "core/cloud_secret.hpp"

#include "duckdb/common/serializer/deserializer.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

#include <memory>
#include <vector>

namespace duckdb {

// ─── Global dispatch table ────────────────────────────────────────────────────
// create_secret_function_t is a raw function pointer (no captures allowed).
// We store each registration's state in a global table keyed by index,
// then generate one non-capturing lambda per slot via a template expansion trick.
// For simplicity: we use a static vector + index-dispatch function.
//
// Thread safety: registrations happen at extension load time (single-threaded),
// invocations happen at CREATE SECRET time (may be concurrent).
// Reads are safe after initial setup because the vector is never grown after Load().
//
// Lifecycle: CloudSecretRegistry::Clear() is called at the start of each
// LoadInternal() to remove stale CloudFileSystem* pointers from prior loads.
// This prevents use-after-free when the extension is reloaded or loaded into
// multiple DuckDB database instances within the same process.

struct SecretDispatchEntry {
    CloudFileSystem* cfs;
    SecretBuilderFn builder;
};

static std::vector<SecretDispatchEntry>& GetDispatchTable() {
    static std::vector<SecretDispatchEntry> table;
    return table;
}

// We generate up to 32 dispatch slots at compile time.
// Each slot is a distinct non-capturing function that reads its state from the global table.
#define MAKE_SLOT(N)                                                                          \
    static unique_ptr<BaseSecret> SecretSlot_##N(ClientContext& ctx, CreateSecretInput& in) { \
        auto& e = GetDispatchTable()[N];                                                      \
        auto result = make_uniq<KeyValueSecret>(in.scope, in.type, in.provider, in.name);     \
        for (auto it = in.options.begin(); it != in.options.end(); ++it)                      \
            result->secret_map[it->first] = it->second;                                       \
        e.builder(ctx, in, *e.cfs);                                                           \
        return std::move(result);                                                             \
    }

MAKE_SLOT(0)
MAKE_SLOT(1)
MAKE_SLOT(2)
MAKE_SLOT(3)
MAKE_SLOT(4) MAKE_SLOT(5) MAKE_SLOT(6) MAKE_SLOT(7) MAKE_SLOT(8) MAKE_SLOT(9) MAKE_SLOT(10)
    MAKE_SLOT(11) MAKE_SLOT(12) MAKE_SLOT(13) MAKE_SLOT(14) MAKE_SLOT(15)

        static create_secret_function_t kSlots[] = {
            SecretSlot_0,  SecretSlot_1,  SecretSlot_2,  SecretSlot_3,
            SecretSlot_4,  SecretSlot_5,  SecretSlot_6,  SecretSlot_7,
            SecretSlot_8,  SecretSlot_9,  SecretSlot_10, SecretSlot_11,
            SecretSlot_12, SecretSlot_13, SecretSlot_14, SecretSlot_15,
};
static constexpr size_t kMaxSlots = sizeof(kSlots) / sizeof(kSlots[0]);

// ─── CloudSecretRegistry::Clear ──────────────────────────────────────────────
// Called at the start of LoadInternal so that re-loading the extension into a
// new DuckDB database instance doesn't accumulate stale entries from the
// previous load (the old CloudFileSystem* pointers are invalid after reload).
void CloudSecretRegistry::Clear() {
    GetDispatchTable().clear();
}

// ─── CloudSecretRegistry::Register ───────────────────────────────────────────
void CloudSecretRegistry::Register(ExtensionLoader& loader, CloudFileSystem& cfs,
                                   const std::string& secret_type, const std::string& provider,
                                   std::vector<std::string> named_params, SecretBuilderFn builder) {
    auto& table = GetDispatchTable();
    if (table.size() >= kMaxSlots)
        throw InternalException("CloudSecretRegistry: exceeded max slot count (" +
                                std::to_string(kMaxSlots) + ")");

    table.push_back({&cfs, std::move(builder)});
    size_t slot_idx = table.size() - 1;

    // Register secret type (idempotent)
    SecretType st;
    st.name = secret_type;
    st.deserializer = +[](Deserializer& d, BaseSecret b) -> unique_ptr<BaseSecret> {
        return KeyValueSecret::Deserialize<KeyValueSecret>(d, std::move(b));
    };
    st.default_provider = provider;
    try {
        loader.RegisterSecretType(st);
    } catch (...) {}

    // Register the CREATE SECRET function using a slot function pointer
    CreateSecretFunction f;
    f.secret_type = secret_type;
    f.provider = provider;
    f.function = kSlots[slot_idx];

    for (auto& p : named_params)
        f.named_parameters[p] = LogicalType::VARCHAR;

    loader.GetDatabaseInstance().GetSecretManager().RegisterSecretFunction(
        std::move(f), OnCreateConflict::REPLACE_ON_CONFLICT);
}

} // namespace duckdb
