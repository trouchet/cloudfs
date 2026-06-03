#define DUCKDB_EXTENSION_MAIN
#include "cloudfs_extension.hpp"

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

// Raw observer pointer; VFS owns the unique_ptr.
static CloudFileSystem* g_cfs = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// LoadInternal
// Wires together: CloudFileSystem + 4 backends + 8 secret providers
// ─────────────────────────────────────────────────────────────────────────────
static void LoadInternal(ExtensionLoader& loader) {
    auto& db = loader.GetDatabaseInstance();

    // Clear stale secret slots from any previous load in this process.
    // Without this, loading the extension into a second DuckDB database would
    // overflow the 16-slot dispatch table.
    CloudSecretRegistry::Clear();

    // ── 1. Create the single CloudFileSystem ─────────────────────────────────
    auto cfs_uptr = make_uniq<CloudFileSystem>();
    g_cfs = cfs_uptr.get();
    db.GetFileSystem().RegisterSubSystem(std::move(cfs_uptr));

    // ── 2. Register all built-in backends ────────────────────────────────────
    // Each backend shares the CloudFileSystem's internal HTTP client.
    // Cast is safe: we own the raw ptr and it outlives all backends.
    g_cfs->RegisterBackend(make_uniq<SharePointBackend>(g_cfs->GetHttpClient()));
    g_cfs->RegisterBackend(make_uniq<OneDriveBackend>(g_cfs->GetHttpClient()));
    g_cfs->RegisterBackend(make_uniq<GDriveBackend>(g_cfs->GetHttpClient()));
    g_cfs->RegisterBackend(make_uniq<DropboxBackend>(g_cfs->GetHttpClient()));
    g_cfs->RegisterBackend(make_uniq<SFTPBackend>());
    g_cfs->RegisterBackend(make_uniq<VFSBackend>(g_cfs->GetHttpClient()));

    // ── 3. Register secret types for each provider ────────────────────────────

    // SharePoint — oauth (Device Code) + token (static)
    CloudSecretRegistry::Register(
        loader, *g_cfs, "sharepoint", "oauth", {"tenant_id", "client_id", "scope"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto get = [&](const std::string& k) {
                auto it = in.options.find(k);
                return it == in.options.end() ? "" : it->second.ToString();
            };
            auto auth = std::make_shared<SharePointDeviceCodeAuth>(
                get("tenant_id").empty() ? "common" : get("tenant_id"),
                get("client_id").empty() ? "04b07795-8542-4c45-a7a4-26c5a3e61e39"
                                         : get("client_id"),
                get("scope").empty() ? "https://graph.microsoft.com/.default" : get("scope"));
            cfs.SetAuth("spfs", auth);
        });

    CloudSecretRegistry::Register(
        loader, *g_cfs, "sharepoint", "token", {"tenant_id", "client_id", "scope", "token"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto tok = in.options.count("token") ? in.options.at("token").ToString() : "";
            if (tok.empty())
                throw InvalidInputException("sharepoint token: TOKEN required");
            cfs.SetAuth("spfs", std::make_shared<StaticTokenAuth>("sharepoint", tok));
        });

    // OneDrive — oauth + token
    CloudSecretRegistry::Register(
        loader, *g_cfs, "onedrive", "oauth", {"tenant_id", "client_id"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto get = [&](const std::string& k) {
                auto it = in.options.find(k);
                return it == in.options.end() ? "" : it->second.ToString();
            };
            auto auth = std::make_shared<OneDriveAuth>(
                get("tenant_id").empty() ? "common" : get("tenant_id"),
                get("client_id").empty() ? "04b07795-8542-4c45-a7a4-26c5a3e61e39"
                                         : get("client_id"));
            cfs.SetAuth("odfs", auth);
        });

    CloudSecretRegistry::Register(
        loader, *g_cfs, "onedrive", "token", {"token"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto tok = in.options.count("token") ? in.options.at("token").ToString() : "";
            if (tok.empty())
                throw InvalidInputException("onedrive token: TOKEN required");
            cfs.SetAuth("odfs", std::make_shared<StaticTokenAuth>("onedrive", tok));
        });

    // Google Drive — oauth (user PKCE) + service_account (JWT)
    CloudSecretRegistry::Register(
        loader, *g_cfs, "gdrive", "oauth", {"client_id", "client_secret"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto get = [&](const std::string& k) {
                auto it = in.options.find(k);
                return it == in.options.end() ? "" : it->second.ToString();
            };
            cfs.SetAuth("gdfs", std::make_shared<GDriveOAuthProvider>(get("client_id"),
                                                                      get("client_secret")));
        });

    CloudSecretRegistry::Register(
        loader, *g_cfs, "gdrive", "service_account", {"key_json"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto key = in.options.count("key_json") ? in.options.at("key_json").ToString() : "";
            if (key.empty())
                throw InvalidInputException("gdrive service_account: KEY_JSON required");
            cfs.SetAuth("gdfs", std::make_shared<GDriveServiceAccountAuth>(key));
        });

    CloudSecretRegistry::Register(
        loader, *g_cfs, "gdrive", "token", {"token"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto tok = in.options.count("token") ? in.options.at("token").ToString() : "";
            if (tok.empty())
                throw InvalidInputException("gdrive token: TOKEN required");
            cfs.SetAuth("gdfs", std::make_shared<StaticTokenAuth>("gdrive", tok));
        });

    // Dropbox — oauth + token
    CloudSecretRegistry::Register(
        loader, *g_cfs, "dropbox", "oauth", {"app_key", "app_secret"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto get = [&](const std::string& k) {
                auto it = in.options.find(k);
                return it == in.options.end() ? "" : it->second.ToString();
            };
            cfs.SetAuth("dbxfs",
                        std::make_shared<DropboxOAuthProvider>(get("app_key"), get("app_secret")));
        });

    CloudSecretRegistry::Register(
        loader, *g_cfs, "dropbox", "token", {"token"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto tok = in.options.count("token") ? in.options.at("token").ToString() : "";
            if (tok.empty())
                throw InvalidInputException("dropbox token: TOKEN required");
            cfs.SetAuth("dbxfs", std::make_shared<StaticTokenAuth>("dropbox", tok));
        });

    // ── SFTP ──────────────────────────────────────────────────────────────────
    // sftp://user@host/path — token encodes auth method:
    //   "keyfile:/home/user/.ssh/id_rsa[:passphrase]"
    //   "agent:"
    //   "password:mysecret"
    CloudSecretRegistry::Register(
        loader, *g_cfs, "sftp", "keyfile", {"host", "user", "key_path", "passphrase"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto get = [&](const std::string& k) {
                auto it = in.options.find(k);
                return it == in.options.end() ? "" : it->second.ToString();
            };
            std::string tok = "keyfile:" + get("key_path");
            if (!get("passphrase").empty())
                tok += ":" + get("passphrase");
            if (tok == "keyfile:")
                throw InvalidInputException("sftp keyfile: KEY_PATH required");
            cfs.SetAuth("sftp", std::make_shared<SFTPAuth>(tok));
        });

    CloudSecretRegistry::Register(loader, *g_cfs, "sftp", "agent", {"host", "user"},
                                  [](ClientContext&, CreateSecretInput&, CloudFileSystem& cfs) {
                                      cfs.SetAuth("sftp", std::make_shared<SFTPAuth>("agent:"));
                                  });

    CloudSecretRegistry::Register(
        loader, *g_cfs, "sftp", "password", {"host", "user", "password"},
        [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
            auto it = in.options.find("password");
            std::string pw = it == in.options.end() ? "" : it->second.ToString();
            if (pw.empty())
                throw InvalidInputException("sftp password: PASSWORD required");
            cfs.SetAuth("sftp", std::make_shared<SFTPAuth>("password:" + pw));
        });

    // ── VFS (cloudfs-agent) ───────────────────────────────────────────────────
    CloudSecretRegistry::Register(loader, *g_cfs, "vfs", "token", {"token"},
                                  [](ClientContext&, CreateSecretInput& in, CloudFileSystem& cfs) {
                                      auto it = in.options.find("token");
                                      std::string tok =
                                          it == in.options.end() ? "" : it->second.ToString();
                                      if (tok.empty())
                                          throw InvalidInputException("vfs: TOKEN required");
                                      cfs.SetAuth("vfs", std::make_shared<VFSAuth>(tok));
                                      cfs.SetAuth("vfs+tls", std::make_shared<VFSAuth>(tok));
                                  });

    // ── 4. Utility scalar functions ───────────────────────────────────────────

    // cloudfs_version() keeps prefix — version() is a DuckDB built-in
    loader.RegisterFunction(ScalarFunction(
        "cloudfs_version", {}, LogicalType::VARCHAR,
        [](DataChunk&, ExpressionState&, Vector& r) { r.SetValue(0, Value("cloudfs 0.1.0")); }));

    // providers() — no prefix, no collision
    {
        ScalarFunctionSet s("providers");
        s.AddFunction(
            ScalarFunction({}, LogicalType::VARCHAR, [](DataChunk&, ExpressionState&, Vector& r) {
                if (!g_cfs) {
                    r.SetValue(0, Value(""));
                    return;
                }
                std::string out;
                for (auto& sc : g_cfs->RegisteredSchemes())
                    out += (out.empty() ? "" : ", ") + sc;
                r.SetValue(0, Value(out));
            }));
        loader.RegisterFunction(s);
    }

    // clear_cache() and clear_cache(scheme) — no prefix, no collision
    {
        ScalarFunctionSet s("clear_cache");
        s.AddFunction(
            ScalarFunction({}, LogicalType::VARCHAR, [](DataChunk&, ExpressionState&, Vector& r) {
                if (g_cfs)
                    g_cfs->GetCache().ClearAll();
                r.SetValue(0, Value("OK"));
            }));
        s.AddFunction(ScalarFunction({LogicalType::VARCHAR}, LogicalType::VARCHAR,
                                     [](DataChunk& args, ExpressionState&, Vector& r) {
                                         auto sc = args.data[0].GetValue(0).ToString();
                                         if (g_cfs) {
                                             if (sc == "*" || sc == "all")
                                                 g_cfs->GetCache().ClearAll();
                                             else
                                                 g_cfs->GetCache().ClearScheme(sc);
                                         }
                                         r.SetValue(0, Value("OK"));
                                     }));
        loader.RegisterFunction(s);
    }

    // Table functions: ls(), stat(), du()
    SetCloudFS(g_cfs);
    RegisterCloudTableFunctions(loader);
}

// ─────────────────────────────────────────────────────────────────────────────
void CloudFsExtension::Load(ExtensionLoader& loader) {
    LoadInternal(loader);
}
std::string CloudFsExtension::Name() {
    return "cloudfs";
}
std::string CloudFsExtension::Version() const {
#ifdef EXT_VERSION_CLOUDFS
    return EXT_VERSION_CLOUDFS;
#else
    return "0.1.0";
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
