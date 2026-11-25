#include "core/cloud_secret.hpp"

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/serializer/deserializer.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/storage/object_cache.hpp"

#include <functional>
#include <unordered_set>

namespace duckdb {

// Options whose values must never be shown by duckdb_secrets().
static const char* const kSensitiveKeys[] = {"token",    "client_secret", "app_secret",
                                             "password", "passphrase",    "key_json"};

// ─── Per-database state ───────────────────────────────────────────────────────
// CREATE SECRET functions are plain function pointers, so they cannot capture
// the registry they belong to.  The registry is published in the database's
// ObjectCache instead, which keeps the lookup per database: several databases
// in one process each find their own registry.
struct CloudfsDatabaseState : public ObjectCacheEntry {
    explicit CloudfsDatabaseState(CloudSecretRegistry& registry_) : registry(registry_) {}

    static string ObjectType() { return "cloudfs_database_state"; }
    string GetObjectType() override { return ObjectType(); }
    // Not evictable: this is a pointer to live state, not a cache entry.
    optional_idx GetEstimatedCacheMemory() const override { return optional_idx(); }

    CloudSecretRegistry& registry;
};

static const char* const kStateKey = "cloudfs_database_state";

static std::string FactoryKey(const std::string& secret_type, const std::string& provider) {
    return StringUtil::Lower(secret_type) + "/" + StringUtil::Lower(provider);
}

// ─── CREATE SECRET ────────────────────────────────────────────────────────────
// One function serves every (type, provider) pair.  It stores the options in a
// KeyValueSecret and validates them by running the matching auth factory; the
// auth provider itself is discarded and rebuilt lazily on first use.
static unique_ptr<BaseSecret> CreateCloudSecret(ClientContext& context, CreateSecretInput& input) {
    auto state = DatabaseInstance::GetDatabase(context).GetObjectCache().Get<CloudfsDatabaseState>(
        kStateKey);
    if (!state)
        throw InternalException("cloudfs: secret registry is not initialized for this database");

    auto secret = make_uniq<KeyValueSecret>(input.scope, input.type, input.provider, input.name);
    SecretParams params;
    for (auto& option : input.options) {
        auto key = StringUtil::Lower(option.first);
        secret->secret_map[key] = option.second;
        params.values[key] = option.second.ToString();
    }
    for (auto& key : kSensitiveKeys)
        secret->redact_keys.insert(key);

    state->registry.Build(input.type, input.provider, params);
    return std::move(secret);
}

// ─── CloudSecretRegistry ──────────────────────────────────────────────────────
CloudSecretRegistry::CloudSecretRegistry(DatabaseInstance& db) : db_(db) {
    db_.GetObjectCache().Put(kStateKey, make_shared_ptr<CloudfsDatabaseState>(*this));
}

void CloudSecretRegistry::Register(ExtensionLoader& loader, const std::string& secret_type,
                                   const std::string& provider,
                                   const std::vector<std::string>& named_params,
                                   AuthFactoryFn factory) {
    // First provider registered for a type: declare the type with it as default.
    bool type_known = false;
    const std::string type_prefix = StringUtil::Lower(secret_type) + "/";
    for (auto& entry : factories_) {
        if (entry.first.compare(0, type_prefix.size(), type_prefix) == 0) {
            type_known = true;
            break;
        }
    }
    if (!type_known) {
        SecretType st;
        st.name = secret_type;
        st.deserializer = +[](Deserializer& d, BaseSecret b) -> unique_ptr<BaseSecret> {
            return KeyValueSecret::Deserialize<KeyValueSecret>(d, std::move(b));
        };
        st.default_provider = provider;
        loader.RegisterSecretType(st);
    }

    AddFactory(secret_type, provider, std::move(factory));

    CreateSecretFunction f;
    f.secret_type = secret_type;
    f.provider = provider;
    f.function = CreateCloudSecret;
    for (auto& p : named_params)
        f.named_parameters[p] = LogicalType::VARCHAR;
    loader.RegisterFunction(std::move(f));
}

void CloudSecretRegistry::AddFactory(const std::string& secret_type, const std::string& provider,
                                     AuthFactoryFn factory) {
    factories_[FactoryKey(secret_type, provider)] = std::move(factory);
}

std::shared_ptr<ICloudAuthProvider> CloudSecretRegistry::Build(const std::string& secret_type,
                                                               const std::string& provider,
                                                               const SecretParams& params) const {
    auto it = factories_.find(FactoryKey(secret_type, provider));
    if (it == factories_.end())
        throw InvalidInputException("cloudfs: no provider '%s' for secret type '%s'", provider,
                                    secret_type);
    auto auth = it->second(params);
    if (!auth)
        throw InternalException("cloudfs: auth factory for %s/%s returned nothing", secret_type,
                                provider);
    return auth;
}

void CloudSecretRegistry::ClearAuthCache() {
    std::lock_guard<std::mutex> lk(mu_);
    auth_cache_.clear();
}

static std::string CacheKey(const SecretEntry& entry) {
    return entry.storage_mode + "/" + entry.secret->GetName();
}

bool CloudSecretRegistry::ResolveAuth(const std::string& url, const std::string& secret_type,
                                      std::shared_ptr<ICloudAuthProvider>& out, std::string& err) {
    try {
        auto& manager = SecretManager::Get(db_);
        auto transaction = CatalogTransaction::GetSystemTransaction(db_);
        auto match = manager.LookupSecret(transaction, url, secret_type);
        if (!match.HasMatch()) {
            err = "cloudfs: no secret matches '" + url + "'. Run: CREATE SECRET (TYPE " +
                  secret_type + ", ...)";
            return false;
        }

        auto* kv = dynamic_cast<const KeyValueSecret*>(match.secret_entry->secret.get());
        if (!kv) {
            err = "cloudfs: secret '" + match.GetSecret().GetName() + "' has an unexpected format";
            return false;
        }

        // The fingerprint covers everything the factory reads, so a secret
        // replaced under the same name gets a fresh auth provider.
        SecretParams params;
        std::string material = kv->GetProvider();
        for (auto& option : kv->secret_map) { // ordered map: stable iteration
            auto key = StringUtil::Lower(option.first);
            auto value = option.second.ToString();
            material += '\x1f' + key + '\x1e' + value;
            params.values[key] = std::move(value);
        }
        const size_t fingerprint = std::hash<std::string>()(material);
        const std::string cache_key = CacheKey(*match.secret_entry);

        std::lock_guard<std::mutex> lk(mu_);
        auto cached = auth_cache_.find(cache_key);
        if (cached != auth_cache_.end() && cached->second.fingerprint == fingerprint) {
            out = cached->second.auth;
            return true;
        }

        auto auth = Build(kv->GetType(), kv->GetProvider(), params);

        // A cache miss is rare (first use or changed secret), so it is a cheap
        // moment to forget the tokens of secrets that no longer exist.
        std::unordered_set<std::string> live;
        for (auto& entry : manager.AllSecrets(transaction))
            live.insert(CacheKey(entry));
        for (auto it = auth_cache_.begin(); it != auth_cache_.end();) {
            if (live.count(it->first))
                ++it;
            else
                it = auth_cache_.erase(it);
        }

        auth_cache_[cache_key] = CachedAuth{fingerprint, auth};
        out = std::move(auth);
        return true;
    } catch (std::exception& ex) {
        ErrorData error(ex);
        err = "cloudfs: cannot use secret for '" + url + "': " + error.RawMessage();
        return false;
    }
}

} // namespace duckdb
