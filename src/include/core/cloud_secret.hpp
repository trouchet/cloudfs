#pragma once
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

#include "cloud_auth.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace duckdb {

// ─────────────────────────────────────────────────────────────────────────────
// SecretParams
// The key/value options of a secret, as plain strings with lower-case keys.
// Built either from CREATE SECRET input or from a stored KeyValueSecret, so an
// auth factory sees the same thing in both cases.
// ─────────────────────────────────────────────────────────────────────────────
struct SecretParams {
    std::unordered_map<std::string, std::string> values;

    // Value for `key`, or "" when absent.
    std::string Get(const std::string& key) const {
        auto it = values.find(key);
        return it == values.end() ? std::string() : it->second;
    }
    // Value for `key`, or `fallback` when absent or empty.
    std::string GetOr(const std::string& key, const std::string& fallback) const {
        auto v = Get(key);
        return v.empty() ? fallback : v;
    }
};

// Builds an auth provider from a secret's options.  Must validate its input
// and throw InvalidInputException on bad or missing options.  Must not do I/O:
// it runs at CREATE SECRET time (to validate) and again lazily on first use.
using AuthFactoryFn = std::function<std::shared_ptr<ICloudAuthProvider>(const SecretParams&)>;

// ─────────────────────────────────────────────────────────────────────────────
// CloudSecretRegistry
//
// Connects DuckDB's secret manager to cloudfs auth providers.  One instance
// per database, owned by that database's CloudFileSystem.
//
// The secret manager is the single source of truth: credentials are looked up
// by path on every access (ResolveAuth), so persistent secrets work after a
// restart, DROP SECRET takes effect immediately and SCOPE selects between
// several secrets of the same type.  CREATE SECRET has no side effect on the
// filesystem; it only validates the options.
//
// Auth providers are cached per secret so OAuth token state survives between
// calls, and rebuilt when the secret's contents change (CREATE OR REPLACE).
// ─────────────────────────────────────────────────────────────────────────────
class CloudSecretRegistry {
  public:
    explicit CloudSecretRegistry(DatabaseInstance& db);

    // Register a (secret_type, provider) pair.  The first provider registered
    // for a type becomes its default.  named_params are the accepted options.
    void Register(ExtensionLoader& loader, const std::string& secret_type,
                  const std::string& provider, const std::vector<std::string>& named_params,
                  AuthFactoryFn factory);

    // Add only the auth factory for a (secret_type, provider) pair, without
    // registering anything with DuckDB.  Register() calls this; it is public
    // for embedders and tests that register the secret type themselves.
    void AddFactory(const std::string& secret_type, const std::string& provider,
                    AuthFactoryFn factory);

    // Find the secret of `secret_type` whose scope best matches `url` and
    // return its auth provider.  Returns false and sets err when no secret
    // matches or the secret cannot be turned into an auth provider.
    bool ResolveAuth(const std::string& url, const std::string& secret_type,
                     std::shared_ptr<ICloudAuthProvider>& out, std::string& err);

    // Run the factory for (secret_type, provider).  Throws on invalid options.
    std::shared_ptr<ICloudAuthProvider> Build(const std::string& secret_type,
                                              const std::string& provider,
                                              const SecretParams& params) const;

    // Drop all cached auth providers (and their tokens).
    void ClearAuthCache();

  private:
    struct CachedAuth {
        size_t fingerprint;
        std::shared_ptr<ICloudAuthProvider> auth;
    };

    DatabaseInstance& db_;
    // "type/provider" → factory.  Written only during extension load.
    std::unordered_map<std::string, AuthFactoryFn> factories_;
    // "storage/name" → auth provider built from that secret.
    std::unordered_map<std::string, CachedAuth> auth_cache_;
    std::mutex mu_;
};

} // namespace duckdb
