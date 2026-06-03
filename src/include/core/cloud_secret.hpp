#pragma once
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "cloud_filesystem.hpp"
#include <functional>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// SecretProviderFactory
//
// Lets each backend register its own CREATE SECRET providers without touching
// CloudFileSystem.  A backend calls RegisterSecretProviders() from its own
// setup code; this function handles the DuckDB API boilerplate.
//
// Usage (inside a backend's Init() function):
//
//   CloudSecretRegistry::Register(loader, "gdfs", "oauth",
//     {"client_id", "client_secret", "scope"},
//     [](ClientContext &ctx, CreateSecretInput &in, CloudFileSystem &cfs) {
//         auto auth = make_shared<GDriveOAuthProvider>(
//             in.options["client_id"].ToString(), ...);
//         cfs.SetAuth("gdfs", auth);
//     });
// ─────────────────────────────────────────────────────────────────────────────

using SecretBuilderFn = std::function<void(ClientContext &,
                                            CreateSecretInput &,
                                            CloudFileSystem &)>;

struct CloudSecretRegistry {
    // Clear all registered slots. Must be called at the start of LoadInternal
    // so re-loading into a new DuckDB database doesn't exhaust the slot array.
    static void Clear();

    // Register a (secret_type, provider) pair backed by a builder callback.
    // named_param_names: list of accepted named parameters (e.g. {"token","scope"})
    static void Register(ExtensionLoader        &loader,
                          CloudFileSystem        &cfs,
                          const std::string      &secret_type,   // e.g. "gdfs"
                          const std::string      &provider,      // e.g. "oauth"
                          std::vector<std::string> named_params,
                          SecretBuilderFn         builder);
};

} // namespace duckdb
