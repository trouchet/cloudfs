#define DUCKDB_EXTENSION_MAIN
#include "cloudfs_extension.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "core/cloud_filesystem.hpp"
#include "core/cloud_secret.hpp"
#include "core/cloud_table_functions.hpp"

#include "providers/dropbox_backend.hpp"
#include "providers/gdrive_backend.hpp"
#include "providers/onedrive_backend.hpp"
#include "providers/sftp_backend.hpp"
#include "providers/sharepoint_backend.hpp"
#include "providers/vfs_backend.hpp"

namespace duckdb {

// Reject a missing required option with a uniform message.
static std::string Require(const SecretParams& p, const char* key, const char* what) {
    auto v = p.Get(key);
    if (v.empty())
        throw InvalidInputException("%s: %s required", what, StringUtil::Upper(key));
    return v;
}

// Fallback client for the Microsoft device-code login when CLIENT_ID is not
// given: Microsoft's first-party Azure CLI application (AZURE_CLI_CLIENT_ID in
// azure-cli-core).  Its Graph permissions are Microsoft's to define and a
// tenant may block device code for it, so docs/CREDENTIALS.md recommends
// registering your own app and passing its CLIENT_ID.
static const char* const kAzureCliClientId = "04b07795-8ddb-461a-bbee-02f9e1bf7b46";
static const char* const kGraphDefaultScope = "https://graph.microsoft.com/.default";

// ─────────────────────────────────────────────────────────────────────────────
// Secret types and providers
//
// Each entry maps CREATE SECRET (TYPE t, PROVIDER p, ...) to a factory that
// turns the secret's options into an auth provider.  Factories run when the
// secret is created (to validate it) and again, lazily, the first time a path
// matching the secret is accessed; see CloudSecretRegistry.
// The first provider registered for a type is its default.
//
// The interactive "oauth" providers also accept PERSIST_TOKEN (BOOLEAN, default
// true): when the secret is PERSISTENT, the refresh token obtained at login is
// stored in it so later processes do not prompt again.  PERSIST_TOKEN false
// keeps that token in memory only.
// ─────────────────────────────────────────────────────────────────────────────
static void RegisterSecretProviders(ExtensionLoader& loader, CloudSecretRegistry& secrets) {
    using AuthPtr = std::shared_ptr<ICloudAuthProvider>;

    // ── SharePoint ────────────────────────────────────────────────────────────
    secrets.Register(
        loader, "sharepoint", "oauth", {"tenant_id", "client_id", "scope", "persist_token"},
        [](const SecretParams& p) -> AuthPtr {
            return std::make_shared<SharePointDeviceCodeAuth>(
                p.GetOr("tenant_id", "common"), p.GetOr("client_id", kAzureCliClientId),
                p.GetOr("scope", kGraphDefaultScope));
        });
    secrets.Register(loader, "sharepoint", "token", {"tenant_id", "client_id", "scope", "token"},
                     [](const SecretParams& p) -> AuthPtr {
                         return std::make_shared<StaticTokenAuth>(
                             "sharepoint", Require(p, "token", "sharepoint token"));
                     });
    secrets.Register(loader, "sharepoint", "client_credentials",
                     {"tenant_id", "client_id", "client_secret", "scope"},
                     [](const SecretParams& p) -> AuthPtr {
                         const char* what = "sharepoint client_credentials";
                         auto tenant = Require(p, "tenant_id", what);
                         auto client = Require(p, "client_id", what);
                         auto secret = Require(p, "client_secret", what);
                         return std::make_shared<SharePointClientCredentialsAuth>(
                             tenant, client, secret, p.GetOr("scope", kGraphDefaultScope));
                     });

    // ── OneDrive ──────────────────────────────────────────────────────────────
    secrets.Register(loader, "onedrive", "oauth", {"tenant_id", "client_id", "persist_token"},
                     [](const SecretParams& p) -> AuthPtr {
                         return std::make_shared<OneDriveAuth>(
                             p.GetOr("tenant_id", "common"),
                             p.GetOr("client_id", kAzureCliClientId));
                     });
    secrets.Register(loader, "onedrive", "token", {"token"}, [](const SecretParams& p) -> AuthPtr {
        return std::make_shared<StaticTokenAuth>("onedrive", Require(p, "token", "onedrive token"));
    });
    secrets.Register(loader, "onedrive", "client_credentials",
                     {"tenant_id", "client_id", "client_secret", "scope"},
                     [](const SecretParams& p) -> AuthPtr {
                         const char* what = "onedrive client_credentials";
                         auto tenant = Require(p, "tenant_id", what);
                         auto client = Require(p, "client_id", what);
                         auto secret = Require(p, "client_secret", what);
                         return std::make_shared<OneDriveClientCredentialsAuth>(
                             tenant, client, secret, p.GetOr("scope", kGraphDefaultScope));
                     });

    // ── Google Drive ──────────────────────────────────────────────────────────
    secrets.Register(loader, "gdrive", "oauth",
                     {"client_id", "client_secret", "redirect_port", "persist_token"},
                     [](const SecretParams& p) -> AuthPtr {
                         // 0 (the default) lets the system pick a free port.
                         int port = 0;
                         auto port_text = p.Get("redirect_port");
                         if (!port_text.empty()) {
                             bool digits = port_text.size() <= 5;
                             for (char c : port_text)
                                 digits = digits && c >= '0' && c <= '9';
                             port = digits ? std::stoi(port_text) : -1;
                             if (port < 0 || port > 65535)
                                 throw InvalidInputException(
                                     "gdrive oauth: REDIRECT_PORT must be a port number (0-65535)");
                         }
                         return std::make_shared<GDriveOAuthProvider>(p.Get("client_id"),
                                                                      p.Get("client_secret"), port);
                     });
    secrets.Register(loader, "gdrive", "service_account", {"key_json"},
                     [](const SecretParams& p) -> AuthPtr {
                         return std::make_shared<GDriveServiceAccountAuth>(
                             Require(p, "key_json", "gdrive service_account"));
                     });
    secrets.Register(loader, "gdrive", "token", {"token"}, [](const SecretParams& p) -> AuthPtr {
        return std::make_shared<StaticTokenAuth>("gdrive", Require(p, "token", "gdrive token"));
    });

    // ── Dropbox ───────────────────────────────────────────────────────────────
    secrets.Register(loader, "dropbox", "oauth", {"app_key", "app_secret", "persist_token"},
                     [](const SecretParams& p) -> AuthPtr {
                         return std::make_shared<DropboxOAuthProvider>(p.Get("app_key"),
                                                                       p.Get("app_secret"));
                     });
    secrets.Register(loader, "dropbox", "token", {"token"}, [](const SecretParams& p) -> AuthPtr {
        return std::make_shared<StaticTokenAuth>("dropbox", Require(p, "token", "dropbox token"));
    });

    // ── SFTP ──────────────────────────────────────────────────────────────────
    // The SFTP backend receives its credentials through the "token" string:
    //   "keyfile:/home/user/.ssh/id_rsa[:passphrase]", "agent:", "password:secret"
    secrets.Register(loader, "sftp", "keyfile", {"host", "user", "key_path", "passphrase"},
                     [](const SecretParams& p) -> AuthPtr {
                         std::string tok = "keyfile:" + Require(p, "key_path", "sftp keyfile");
                         if (!p.Get("passphrase").empty())
                             tok += ":" + p.Get("passphrase");
                         return std::make_shared<SFTPAuth>(tok);
                     });
    secrets.Register(loader, "sftp", "agent", {"host", "user"}, [](const SecretParams&) -> AuthPtr {
        return std::make_shared<SFTPAuth>("agent:");
    });
    secrets.Register(loader, "sftp", "password", {"host", "user", "password"},
                     [](const SecretParams& p) -> AuthPtr {
                         return std::make_shared<SFTPAuth>("password:" +
                                                           Require(p, "password", "sftp password"));
                     });

    // ── VFS (cloudfs-agent) ───────────────────────────────────────────────────
    secrets.Register(loader, "vfs", "token", {"token"}, [](const SecretParams& p) -> AuthPtr {
        return std::make_shared<VFSAuth>(Require(p, "token", "vfs"));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadInternal
// Wires together the CloudFileSystem, its backends, the secret providers and
// the SQL functions.  Everything is owned by the database being loaded into:
// there is no process-global state, so several databases can load the
// extension side by side.
// ─────────────────────────────────────────────────────────────────────────────
static void LoadInternal(ExtensionLoader& loader) {
    auto& db = loader.GetDatabaseInstance();

    // ── 1. Create the CloudFileSystem ─────────────────────────────────────────
    // The database's VirtualFileSystem owns it; the raw pointer captured by the
    // functions below stays valid for as long as those functions exist.
    auto cfs_uptr = make_uniq<CloudFileSystem>(db);
    CloudFileSystem* cfs = cfs_uptr.get();
    db.GetFileSystem().RegisterSubSystem(std::move(cfs_uptr));

    // ── 2. Register all built-in backends ────────────────────────────────────
    // Each backend shares the CloudFileSystem's internal HTTP client.
    cfs->RegisterBackend(make_uniq<SharePointBackend>(cfs->GetHttpClient()));
    cfs->RegisterBackend(make_uniq<OneDriveBackend>(cfs->GetHttpClient()));
    cfs->RegisterBackend(make_uniq<GDriveBackend>(cfs->GetHttpClient()));
    cfs->RegisterBackend(make_uniq<DropboxBackend>(cfs->GetHttpClient()));
    cfs->RegisterBackend(make_uniq<SFTPBackend>());
    cfs->RegisterBackend(make_uniq<VFSBackend>(cfs->GetHttpClient()));

    // ── 3. Register secret types for each provider ────────────────────────────
    RegisterSecretProviders(loader, cfs->GetSecrets());

    // ── 4. Utility scalar functions ───────────────────────────────────────────

    // cloudfs_version() keeps prefix — version() is a DuckDB built-in
    loader.RegisterFunction(ScalarFunction(
        "cloudfs_version", {}, LogicalType::VARCHAR, [](DataChunk&, ExpressionState&, Vector& r) {
            r.SetValue(0, Value("cloudfs " + CloudfsExtension().Version()));
        }));

    // cloudfs_providers()
    {
        ScalarFunctionSet s("cloudfs_providers");
        s.AddFunction(ScalarFunction({}, LogicalType::VARCHAR,
                                     [cfs](DataChunk&, ExpressionState&, Vector& r) {
                                         std::string out;
                                         for (auto& sc : cfs->RegisteredSchemes())
                                             out += (out.empty() ? "" : ", ") + sc;
                                         r.SetValue(0, Value(out));
                                     }));
        loader.RegisterFunction(s);
    }

    // cloudfs_clear_cache() and cloudfs_clear_cache(scheme)
    {
        ScalarFunctionSet s("cloudfs_clear_cache");
        s.AddFunction(ScalarFunction({}, LogicalType::VARCHAR,
                                     [cfs](DataChunk&, ExpressionState&, Vector& r) {
                                         cfs->GetCache().ClearAll();
                                         r.SetValue(0, Value("OK: all caches cleared"));
                                     }));
        s.AddFunction(ScalarFunction({LogicalType::VARCHAR}, LogicalType::VARCHAR,
                                     [cfs](DataChunk& args, ExpressionState&, Vector& r) {
                                         auto sc = args.data[0].GetValue(0).ToString();
                                         if (sc == "*" || sc == "all")
                                             cfs->GetCache().ClearAll();
                                         else
                                             cfs->GetCache().ClearScheme(sc);
                                         r.SetValue(0, Value("OK: cache cleared for '" + sc + "'"));
                                     }));
        loader.RegisterFunction(s);
    }

    // Table functions: ls(), stat(), du()
    RegisterCloudTableFunctions(loader, cfs);
}

// ─────────────────────────────────────────────────────────────────────────────
void CloudfsExtension::Load(ExtensionLoader& loader) {
    LoadInternal(loader);
}
std::string CloudfsExtension::Name() {
    return "cloudfs";
}
std::string CloudfsExtension::Version() const {
#ifdef EXT_VERSION_CLOUDFS
    return EXT_VERSION_CLOUDFS;
#else
    return "v0.2.0";
#endif
}

} // namespace duckdb

extern "C" {
DUCKDB_EXTENSION_API void cloudfs_duckdb_cpp_init(duckdb::ExtensionLoader& loader) {
    duckdb::LoadInternal(loader);
}
DUCKDB_EXTENSION_API const char* cloudfs_version() {
    return duckdb::DuckDB::LibraryVersion();
}
} // extern "C"
