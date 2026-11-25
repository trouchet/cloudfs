// Tests that an OAuth refresh token obtained by one database is written back
// into its PERSISTENT secret and reused by the next database, so the user is
// not asked to authorize again.  Uses a fake OAuth provider; no network.
//
// Needs a finished release build:  make release && make -C test/cpp oauth

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

#include "core/cloud_secret.hpp"

#include "duckdb.hpp"

#include <filesystem>
#include <iostream>
#include <random>

using namespace duckdb;

static int g_failures = 0;
#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        }                                                             \
    } while (0)

// Counts interactive authorizations and refreshes across all instances.
struct Counters {
    int acquired = 0;
    int refreshed = 0;
    bool rotate = false;         // hand out a new refresh token on every refresh
    std::string accept = "RT-1"; // the refresh token the "server" accepts
};

class FakeOAuth : public OAuth2AuthBase {
  public:
    explicit FakeOAuth(Counters& c) : OAuth2AuthBase("fake"), c_(c) {}

  protected:
    bool AcquireToken(std::string&) override {
        ++c_.acquired;
        token_.SetFromResponse("AT-interactive", c_.accept, 3600);
        return true;
    }
    bool RefreshToken(std::string& err) override {
        if (token_.refresh_token != c_.accept) {
            err = "invalid_grant";
            return false;
        }
        ++c_.refreshed;
        if (c_.rotate)
            c_.accept = "RT-" + std::to_string(c_.refreshed + 1);
        token_.SetFromResponse("AT-refreshed", c_.accept, 3600);
        return true;
    }

  private:
    Counters& c_;
};

static const char* const kType = "cloudfs_oauth_test";
static const char* const kUrl = "fake://host/file";

// A database whose secrets live in `secret_dir`, with its own registry.
struct Instance {
    DBConfig config; // declared first: initialised before db uses it
    DuckDB db;
    CloudSecretRegistry registry;

    static DBConfig* Prepare(DBConfig& cfg, const std::string& secret_dir) {
        cfg.SetOptionByName("secret_directory", Value(secret_dir));
        return &cfg;
    }

    Instance(const std::string& secret_dir, Counters& counters)
        : db(nullptr, Prepare(config, secret_dir)), registry(*db.instance) {
        SecretType st;
        st.name = kType;
        st.default_provider = "oauth";
        st.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
        SecretManager::Get(*db.instance).RegisterSecretType(st);
        registry.AddFactory(kType, "oauth", [&counters](const SecretParams&) {
            return std::static_pointer_cast<ICloudAuthProvider>(
                std::make_shared<FakeOAuth>(counters));
        });
    }

    void CreateSecret(const std::string& name, SecretPersistType persist) {
        auto secret = make_uniq<KeyValueSecret>(vector<string>{}, kType, "oauth", name);
        secret->secret_map["client_id"] = Value("cid");
        SecretManager::Get(*db.instance)
            .RegisterSecret(CatalogTransaction::GetSystemTransaction(*db.instance),
                            std::move(secret), OnCreateConflict::ERROR_ON_CONFLICT, persist);
    }

    std::string Token() {
        std::string tok, err;
        if (!registry.GetAccessToken(kUrl, kType, tok, err))
            return "ERROR: " + err;
        return tok;
    }

    // The secret as duckdb_secrets() would display it.
    std::string Redacted(const std::string& name) {
        auto entry =
            SecretManager::Get(*db.instance)
                .GetSecretByName(CatalogTransaction::GetSystemTransaction(*db.instance), name);
        return entry ? entry->secret->ToString(SecretDisplayType::REDACTED) : "<no secret>";
    }

    // Value stored under `key` in the named secret, "" if absent.
    std::string Stored(const std::string& name, const std::string& key) {
        auto entry =
            SecretManager::Get(*db.instance)
                .GetSecretByName(CatalogTransaction::GetSystemTransaction(*db.instance), name);
        if (!entry)
            return "<no secret>";
        auto& kv = dynamic_cast<const KeyValueSecret&>(*entry->secret);
        Value v;
        return kv.TryGetValue(key, v) ? v.ToString() : "";
    }
};

int main() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() /
                          ("cloudfs_oauth_test_" + std::to_string(std::random_device()()));
    fs::remove_all(base);

    { // 1. persistent secret: authorize once, then refresh silently in a new database
        const std::string dir = (base / "persistent").string();
        Counters c;
        {
            Instance first(dir, c);
            first.CreateSecret("s", SecretPersistType::PERSISTENT);
            CHECK(first.Token() == "AT-interactive");
            CHECK(c.acquired == 1 && c.refreshed == 0);
            CHECK(first.Stored("s", "refresh_token") == "RT-1");
            CHECK(first.Stored("s", "client_id") == "cid");               // options untouched
            CHECK(first.Redacted("s").find("RT-1") == std::string::npos); // never displayed
            CHECK(first.Token() == "AT-interactive"); // cached provider, no new login
            CHECK(c.acquired == 1);
        }
        {
            Instance second(dir, c);
            CHECK(second.Stored("s", "refresh_token") == "RT-1"); // survived on disk
            CHECK(second.Token() == "AT-refreshed");
            CHECK(c.acquired == 1); // no second interactive authorization
            CHECK(c.refreshed == 1);
        }
    }

    { // 2. rotation: a new refresh token issued on refresh is written back
        const std::string dir = (base / "rotating").string();
        Counters c;
        c.rotate = true;
        {
            Instance first(dir, c);
            first.CreateSecret("s", SecretPersistType::PERSISTENT);
            CHECK(first.Token() == "AT-interactive");
            CHECK(first.Stored("s", "refresh_token") == "RT-1");
        }
        {
            Instance second(dir, c);
            CHECK(second.Token() == "AT-refreshed");
            CHECK(c.accept == "RT-2");
            CHECK(second.Stored("s", "refresh_token") == "RT-2");
        }
        {
            Instance third(dir, c);
            CHECK(third.Token() == "AT-refreshed"); // the rotated token works
            CHECK(c.acquired == 1 && c.refreshed == 2);
        }
    }

    { // 3. a stored token the server no longer accepts falls back to a new login
        const std::string dir = (base / "revoked").string();
        Counters c;
        {
            Instance first(dir, c);
            first.CreateSecret("s", SecretPersistType::PERSISTENT);
            CHECK(first.Token() == "AT-interactive");
        }
        c.accept = "RT-new"; // server-side revocation
        {
            Instance second(dir, c);
            CHECK(second.Token() == "AT-interactive");
            CHECK(c.acquired == 2);
            CHECK(second.Stored("s", "refresh_token") == "RT-new");
        }
    }

    { // 4. temporary secrets are not written to
        const std::string dir = (base / "temporary").string();
        Counters c;
        Instance only(dir, c);
        only.CreateSecret("t", SecretPersistType::TEMPORARY);
        CHECK(only.Token() == "AT-interactive");
        CHECK(only.Stored("t", "refresh_token").empty());
        CHECK(only.Token() == "AT-interactive");
        CHECK(c.acquired == 1);
    }

    fs::remove_all(base);
    std::cout << (g_failures ? "FAILED" : "test_oauth_persistence: all checks passed") << "\n";
    return g_failures ? 1 : 0;
}
