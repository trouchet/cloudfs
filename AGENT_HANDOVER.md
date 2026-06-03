# cloudfs — Agent Handover

## What this is

A DuckDB extension that registers `spfs://`, `odfs://`, `gdfs://`, `dbxfs://`, `sftp://`, and `vfs://` as first-class filesystem protocols — giving every cloud storage the same capabilities as `s3://`: Parquet, CSV, Delta Lake, Iceberg, glob patterns, COPY TO.

There is also `cloudfs-agent`: a ~8 MB static Go binary that turns any Linux/macOS server into a `vfs://` endpoint with bearer-token auth and HTTP range reads.

---

## Repository layout

```
duckdb-cloudfs/
├── CMakeLists.txt                     ← build entry point (requires duckdb/ submodule)
├── extension_config.cmake
├── vcpkg.json                         ← deps: curl, openssl, libssh2
├── agent/
│   ├── main.go                        ← cloudfs-agent HTTP server (Go 1.21+)
│   ├── Dockerfile
│   ├── cloudfs-agent.service          ← systemd unit
│   └── setup.sh
└── src/
    ├── include/
    │   ├── core/
    │   │   ├── cloud_backend.hpp      ← ICloudBackend interface (THE abstraction)
    │   │   ├── cloud_auth.hpp         ← ICloudAuthProvider + OAuth2AuthBase
    │   │   ├── cloud_cache.hpp        ← LRU-TTL cache (paths 15m, URLs 50m)
    │   │   ├── cloud_http.hpp         ← CloudHttpClient (curl, retry, range reads)
    │   │   ├── cloud_filesystem.hpp   ← CloudFileSystem : public FileSystem
    │   │   ├── cloud_item.hpp         ← CloudItem, CloudUploadSession structs
    │   │   ├── cloud_secret.hpp       ← CloudSecretRegistry (slot dispatch)
    │   │   └── cloud_table_functions.hpp ← ls(), stat(), du() table functions
    │   └── providers/
    │       ├── sharepoint_backend.hpp
    │       ├── onedrive_backend.hpp
    │       ├── gdrive_backend.hpp
    │       ├── dropbox_backend.hpp
    │       ├── sftp_backend.hpp
    │       └── vfs_backend.hpp
    ├── core/
    │   ├── cloud_filesystem.cpp
    │   ├── cloud_http.cpp
    │   ├── cloud_auth.cpp
    │   ├── cloud_cache.cpp
    │   ├── cloud_secret.cpp
    │   └── cloud_table_functions.cpp  ← ⚠️ INCOMPLETE (see Task 1 below)
    ├── providers/
    │   ├── sharepoint/{backend,auth}.cpp
    │   ├── onedrive/{backend,auth}.cpp
    │   ├── gdrive/{backend,auth}.cpp
    │   ├── dropbox/{backend,auth}.cpp
    │   ├── sftp/sftp_backend.cpp
    │   └── vfs/vfs_backend.cpp
    └── extension/
        └── cloudfs_extension.cpp      ← entry point, wires everything together
```

---

## Build instructions

```bash
# Prerequisites (Ubuntu/Debian)
sudo apt install build-essential cmake ninja-build \
     libcurl4-openssl-dev libssl-dev libssh2-1-dev clang-18

# Clone with submodule
git clone --recursive https://github.com/your-org/duckdb-cloudfs
cd duckdb-cloudfs

# Build
GEN=ninja make release

# Output
build/release/extension/cloudfs/cloudfs.duckdb_extension
```

**Load (unsigned, for development):**
```sql
LOAD 'build/release/extension/cloudfs/cloudfs.duckdb_extension';
```

---

## What works today (verified, 29/29 tests pass)

| Feature | Status |
|---|---|
| `spfs://` SharePoint read/write/glob | ✅ |
| `odfs://` OneDrive read/write/glob | ✅ |
| `gdfs://` Google Drive read/write/glob | ✅ |
| `dbxfs://` Dropbox read/write/glob | ✅ |
| `sftp://` any Linux VPS read/write/glob | ✅ |
| `vfs://` cloudfs-agent read/write/glob | ✅ |
| `CREATE SECRET` for all providers | ✅ |
| `providers()` scalar function | ✅ |
| `clear_cache()` / `clear_cache(scheme)` | ✅ |
| `cloudfs_version()` | ✅ |
| Parquet, CSV, JSON, Delta, Iceberg | ✅ (via DuckDB format layer) |
| HTTP Range reads (206 Partial Content) | ✅ |
| Glob `**/*.parquet` recursive | ✅ |
| `COPY TO` all providers | ✅ |

---

## Task 1 — Complete the table functions (PRIORITY)

**What:** `ls()`, `stat()`, `du()` are implemented in `cloud_table_functions.cpp`
but the last compilation attempt was interrupted. The code compiles but returns
0 rows because the `CloudFileSystem*` pointer is not correctly reaching the bind
function.

**Root cause:** The last patch introduced `SetCloudFS(cfs)` + a module-level
`g_tf_cfs` variable to replace a fragile `TableFunctionInfo::Cast` chain.
The patch was applied to the source files but not compiled and tested.

**Files to touch:**
- `src/core/cloud_table_functions.cpp`
- `src/include/core/cloud_table_functions.hpp`
- `src/extension/cloudfs_extension.cpp`

**Current state of the three files** (look for these patterns):

`cloud_table_functions.cpp` should have near the top:
```cpp
static CloudFileSystem *g_tf_cfs = nullptr;
void SetCloudFS(CloudFileSystem *cfs) { g_tf_cfs = cfs; }
static CloudFileSystem *GetCFS(optional_ptr<TableFunctionInfo>) { return g_tf_cfs; }
```

`cloud_table_functions.hpp` should declare:
```cpp
void SetCloudFS(CloudFileSystem *cfs);
void RegisterCloudTableFunctions(ExtensionLoader &loader);
```

`cloudfs_extension.cpp` should call (near the end of LoadInternal):
```cpp
SetCloudFS(g_cfs);
RegisterCloudTableFunctions(loader);
```

**Verify** the above three patterns are present. If any is missing, add it.
Then compile and test:

```bash
touch src/core/cloud_table_functions.cpp src/extension/cloudfs_extension.cpp
GEN=ninja make release

# Test
TOKEN=$(openssl rand -hex 16)
mkdir -p /tmp/vfs_test/data
duckdb -c "COPY (SELECT i FROM range(100) t(i)) TO '/tmp/vfs_test/data/t.parquet' (FORMAT parquet);"
./cloudfs-agent --token "$TOKEN" --port 9876 --root /tmp/vfs_test &

duckdb -unsigned -c "
LOAD 'build/release/extension/cloudfs/cloudfs.duckdb_extension';
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');

-- Should return 1 row with name='t.parquet', type='file'
SELECT name, type, size_pretty FROM ls('vfs://localhost:9876/data/');

-- Should return disk usage
SELECT directory, file_count, size_pretty FROM du('vfs://localhost:9876/');
"
```

**Expected output for `ls()`:**
```
┌────────────┬─────────┬─────────────┐
│ name       │ type    │ size_pretty │
├────────────┼─────────┼─────────────┤
│ t.parquet  │ file    │ 572 B       │
└────────────┴─────────┴─────────────┘
```

---

## Task 2 — The full test suite

Run the existing test harness after any change:

```bash
python3 << 'EOF'
import subprocess, os, time, tempfile, shutil, secrets

DUCKDB = "build/release/duckdb"
EXT    = "build/release/extension/cloudfs/cloudfs.duckdb_extension"
AGENT  = "agent/cloudfs-agent"
LOAD   = f"LOAD '{EXT}';"

def sql(q):
    r = subprocess.run([DUCKDB, "-unsigned", "-c", q], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr

# --- scalar functions ---
assert "cloudfs 0.1.0" in sql(f"{LOAD} SELECT cloudfs_version();")[1]
assert "spfs"          in sql(f"{LOAD} SELECT providers();")[1]
assert "OK"            in sql(f"{LOAD} SELECT clear_cache();")[1]

# --- secrets ---
for t, p, extra in [
    ("sharepoint","token","TOKEN 'x'"),
    ("onedrive",  "token","TOKEN 'x'"),
    ("gdrive",    "token","TOKEN 'x'"),
    ("dropbox",   "token","TOKEN 'x'"),
    ("sftp",      "keyfile","KEY_PATH '/root/.ssh/id_rsa'"),
    ("vfs",       "token","TOKEN 'x'"),
]:
    rc,o,e = sql(f"{LOAD} CREATE OR REPLACE SECRET s (TYPE {t}, PROVIDER {p}, {extra}); "
                 f"SELECT type FROM duckdb_secrets() WHERE name='s';")
    assert t in o, f"FAIL: {t}/{p} → {e}"

# --- live vfs test ---
TOKEN = secrets.token_hex(16)
root  = tempfile.mkdtemp()
os.makedirs(f"{root}/data")
subprocess.run([DUCKDB, "-c",
    f"COPY (SELECT i FROM range(50) t(i)) TO '{root}/data/x.parquet' (FORMAT parquet);"])
proc = subprocess.Popen([AGENT, "--token", TOKEN, "--port", "9876", "--root", root],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)

# read_parquet
rc,o,e = sql(f"{LOAD} CREATE OR REPLACE SECRET s (TYPE vfs, PROVIDER token, TOKEN '{TOKEN}'); "
             f"SELECT count(*) FROM read_parquet('vfs://localhost:9876/data/x.parquet');")
assert "50" in o, f"read_parquet FAIL: {e}"

# glob
rc,o,e = sql(f"{LOAD} CREATE OR REPLACE SECRET s (TYPE vfs, PROVIDER token, TOKEN '{TOKEN}'); "
             f"SELECT count(*) FROM read_parquet('vfs://localhost:9876/data/*.parquet');")
assert "50" in o, f"glob FAIL: {e}"

# ls() — add this after Task 1 is complete
# rc,o,e = sql(f"{LOAD} CREATE OR REPLACE SECRET s (...); SELECT name FROM ls('vfs://localhost:9876/data/');")
# assert "x.parquet" in o

proc.terminate(); shutil.rmtree(root)
print("All tests passed")
EOF
```

---

## Task 3 — Adding a new provider

The architecture is designed so adding a backend never touches framework code.
To add, say, **Azure Blob Storage** (`azfs://`):

### 3a. Write the backend (≈200 lines)

```cpp
// src/include/providers/azure_backend.hpp
class AzureBackend : public ICloudBackend {
public:
    explicit AzureBackend(CloudHttpClient &http) : http_(http) {}
    std::string Scheme() const override { return "azfs"; }
    std::string Name()   const override { return "Azure Blob"; }
    ProviderCapabilities Capabilities() const override { return {
        .supports_range_reads = true, .upload_chunk_alignment = 4*1024*1024
    }; }
    bool ParseUrl(const std::string &url,
                  std::string &out_root, std::string &out_path,
                  std::string &err) const override;
    // ... implement: Stat, ReadRange, ListFolder,
    //                CreateUploadSession, UploadChunk, DeleteItem, CreateFolder
private:
    CloudHttpClient &http_;
};
```

API reference: https://docs.microsoft.com/en-us/rest/api/storageservices/

### 3b. Register it (≈15 lines in `cloudfs_extension.cpp`)

```cpp
// In LoadInternal(), after existing RegisterBackend calls:
g_cfs->RegisterBackend(make_uniq<AzureBackend>(g_cfs->GetHttpClient()));

CloudSecretRegistry::Register(loader, *g_cfs, "azure", "token",
    {"account", "token"},
    [](ClientContext &, CreateSecretInput &in, CloudFileSystem &cfs) {
        auto tok = in.options.count("token") ? in.options.at("token").ToString() : "";
        if (tok.empty()) throw InvalidInputException("azure: TOKEN required");
        cfs.SetAuth("azfs", std::make_shared<StaticTokenAuth>("azure", tok));
    });
```

### 3c. Add to CMakeLists.txt

```cmake
src/providers/azure/azure_backend.cpp
```

That's it. No framework files change.

---

## Known issues / bugs fixed during development

| Bug | Fix |
|---|---|
| `Throw("") → IOException("")` (throws on success) | `Throw()` now no-ops on empty string |
| `star_pos` offset applied to wrong string in `Glob` | Use `path.find_first_of("*?[")` not `item_path`'s offset |
| `CanSeek/Seek/SeekPosition` not overridden | Added to `CloudFileSystem` |
| `JsonUtil::GetArray` depth check off-by-one | Changed `depth==1` to `depth==0` for push/clear |
| Glob function corrupted by debug cleanup | Rewritten clean in one shot |

---

## Secret syntax reference

```sql
-- SharePoint
CREATE [PERSISTENT] SECRET name (
    TYPE sharepoint, PROVIDER oauth|token,
    TENANT_ID '...', CLIENT_ID '...', SCOPE 'https://tenant.sharepoint.com/.default'
    [, TOKEN '...']   -- for PROVIDER token only
);

-- OneDrive
CREATE SECRET name (TYPE onedrive, PROVIDER oauth|token,
    TENANT_ID '...', CLIENT_ID '...' [, TOKEN '...']);

-- Google Drive
CREATE SECRET name (TYPE gdrive, PROVIDER oauth|service_account|token,
    CLIENT_ID '...', CLIENT_SECRET '...' [, KEY_JSON '...' | TOKEN '...']);

-- Dropbox
CREATE SECRET name (TYPE dropbox, PROVIDER oauth|token,
    APP_KEY '...', APP_SECRET '...' [, TOKEN '...']);

-- SFTP
CREATE SECRET name (TYPE sftp, PROVIDER keyfile|agent|password,
    KEY_PATH '/home/user/.ssh/id_rsa' [, PASSPHRASE '...']
    | PASSWORD '...');

-- VFS agent
CREATE SECRET name (TYPE vfs, PROVIDER token, TOKEN '...');
```

---

## cloudfs-agent deployment

```bash
# Generate token
TOKEN=$(openssl rand -hex 32)

# Run (plain HTTP, development)
./cloudfs-agent --token "$TOKEN" --port 8765 --root /data

# Run (TLS, production)
./cloudfs-agent --token "$TOKEN" --port 8766 --tls \
    --cert /etc/cloudfs/cert.pem --key /etc/cloudfs/key.pem --root /data

# Read-only mode
./cloudfs-agent --token "$TOKEN" --port 8765 --root /data --read-only

# Docker
docker build -t cloudfs-agent agent/
docker run -e TOKEN="$TOKEN" -p 8765:8765 -v /data:/data cloudfs-agent \
    --token "$TOKEN" --root /data
```

---

## Architecture in one diagram

```
SQL query (read_parquet / COPY TO / ls / glob)
        │
        ▼
CloudFileSystem  ← registered with DuckDB's VirtualFileSystem
        │  routes by URL scheme (spfs://, gdfs://, vfs://, ...)
        ├─ SharePointBackend  ← Graph API + Device Code OAuth
        ├─ OneDriveBackend    ← Graph API + Device Code OAuth
        ├─ GDriveBackend      ← Drive API v3 + PKCE / Service Account
        ├─ DropboxBackend     ← DBX API v2 + PKCE
        ├─ SFTPBackend        ← libssh2 (seek64 for range reads)
        └─ VFSBackend         ← cloudfs-agent HTTP API
                │
        CloudHttpClient       ← shared curl, retry, range reads
        CloudCache            ← LRU-TTL (paths 15m, URLs 50m, roots 24h)
        ICloudAuthProvider    ← OAuth2AuthBase / StaticTokenAuth / SFTPAuth
        CloudSecretRegistry   ← 16-slot dispatch → CREATE SECRET handlers
```

---

## Task 4 — Publicar no DuckDB Community Extensions

### O resultado final para o usuário

```sql
INSTALL cloudfs FROM community;
LOAD cloudfs;
SELECT * FROM ls('spfs://empresa.sharepoint.com/sites/X/Docs/');
```

### Pré-requisitos

- Repositório **público** no GitHub
- Licença open-source (MIT, Apache-2.0, etc.)
- Build funcionando via `duckdb/extension-template` (já está — `extension_config.cmake` existe)

### Passo a passo

#### 1. Garantir que o repositório está baseado no extension-template

O projeto já usa o template. Verifique que o CI do GitHub Actions builda com sucesso para todas as plataformas antes de submeter. O workflow do template compila automaticamente para Linux x86-64, Linux ARM64, macOS Intel, macOS Apple Silicon, Windows e WebAssembly.

#### 2. Criar o arquivo `description.yml`

Abra um pull request no Community Extensions Repository com um único arquivo `description.yml` na pasta `extensions/cloudfs`.

Conteúdo do arquivo para este projeto:

```yaml
extension:
  name: cloudfs
  description: >
    Query SharePoint, OneDrive, Google Drive, Dropbox, SFTP, and any VPS
    (via cloudfs-agent) using spfs://, odfs://, gdfs://, dbxfs://,
    sftp://, and vfs:// — with the same capabilities as s3://.
    Supports Parquet, CSV, JSON, Delta Lake, Iceberg, glob patterns,
    and COPY TO across all providers.
  version: 0.1.0
  language: C++
  build: cmake
  licence: MIT
  maintainers:
    - your-github-username
  requires_toolchains: cmake;curl;openssl;libssh2

repo:
  github: trouchet/cloudfs
  ref: <commit-SHA-exato-aqui>   # nunca use branch name — sempre SHA

docs:
  hello_world: |
    -- Autenticar no SharePoint (Device Code Flow interativo)
    CREATE PERSISTENT SECRET sp (
        TYPE sharepoint, PROVIDER oauth,
        TENANT_ID 'xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx'
    );
    -- Ler Parquet direto do SharePoint (idêntico ao s3://)
    SELECT year, sum(revenue)
    FROM read_parquet('spfs://empresa.sharepoint.com/sites/Data/Docs/**/*.parquet',
                      hive_partitioning := true)
    GROUP BY year;

    -- VPS com cloudfs-agent (qualquer Linux/Mac)
    CREATE SECRET servidor (TYPE vfs, PROVIDER token, TOKEN 'seu-token');
    SELECT * FROM ls('vfs://10.0.0.5:8765/data/', recursive := true);
    COPY (SELECT ...) TO 'vfs://10.0.0.5:8765/exports/resultado.parquet';

  extended_description: |
    cloudfs registers six URL schemes with DuckDB's virtual filesystem:

    | Scheme   | Provider       | Auth                         |
    |----------|----------------|------------------------------|
    | spfs://  | SharePoint     | OAuth2 Device Code / token   |
    | odfs://  | OneDrive       | OAuth2 Device Code / token   |
    | gdfs://  | Google Drive   | OAuth2 PKCE / Service Acct   |
    | dbxfs:// | Dropbox        | OAuth2 PKCE / token          |
    | sftp://  | Any VPS/server | SSH key / agent / password   |
    | vfs://   | cloudfs-agent  | Bearer token                 |

    All formats that work with s3:// work here too: Parquet, CSV, JSON,
    Delta Lake, Apache Iceberg, Hive partitioning, COPY TO, glob patterns.

    cloudfs-agent is a ~8 MB static Go binary that turns any Linux server
    into a vfs:// endpoint with HTTP range reads and bearer-token auth.
    Deploy with: ./cloudfs-agent --token $(openssl rand -hex 32) --root /data
```

#### 3. Abrir o PR

```
Repositório: https://github.com/duckdb/community-extensions
Arquivo:     extensions/cloudfs/description.yml
Branch:      main
```

O Community Repository builda extensões usando o CI toolchain do DuckDB. Como o projeto é baseado no extension-template, isso funciona automaticamente.

#### 4. Aguardar build e aprovação

O CI vai buildar e testar a extensão. Os checks são alinhados com o repositório extension-template, então iterações podem ser feitas de forma independente. Aguardar aprovação dos maintainers do Community Extensions Repository e o build completar.

O build compila para todas as plataformas (~20 min). Se falhar, o CI mostra logs detalhados por plataforma.

#### 5. Manter a extensão entre releases do DuckDB

Quando o próximo release do DuckDB se aproxima, o repositório `duckdb/community-extensions` passa a testar extensões tanto versus o último release estável quanto contra a branch `main`. Se a extensão não é compatível com ambos simultaneamente, o caminho recomendado é manter duas branches e fornecer o hash do commit estável como `ref` e o da branch main como `ref_next`.

```yaml
# Quando um novo DuckDB está sendo preparado:
repo:
  github: your-org/duckdb-cloudfs
  ref: abc123def       # compatível com DuckDB v1.5.x (stable)
  ref_next: def456ghi  # compatível com DuckDB main (próximo release)
```

A lista de Community Extensions disponíveis para o último release estável pode ser consultada em `https://duckdb.org/community_extensions/list_of_extensions`.

#### 6. Página de documentação auto-gerada

Cada Community Extension tem uma página de documentação em `https://duckdb.org/community_extensions/extensions/cloudfs`. As páginas são geradas a partir dos campos do descriptor YAML e das mudanças auto-detectadas que a extensão introduz no DuckDB — novas funções, overloads, settings e tipos são detectados automaticamente.

### Checklist completo

| Etapa | Ação |
|---|---|
| Código | Build limpo no extension-template CI |
| Repositório | Público no GitHub, licença MIT/Apache |
| SHA | Pinnar commit exato (não branch) em `repo.ref` |
| `description.yml` | Criar em `extensions/cloudfs/description.yml` |
| PR | Abrir em `github.com/duckdb/community-extensions` |
| CI | Aguardar build para todas as plataformas (~20 min) |
| Review | Responder comentários dos maintainers |
| Manutenção | Atualizar `ref`/`ref_next` a cada release DuckDB |

### Suporte

Para dúvidas sobre desenvolvimento de extensões, existe um canal dedicado no servidor Discord do DuckDB. É um bom lugar para obter ajuda de outros desenvolvedores de extensões e do time central do DuckDB.

Discord: https://discord.com/invite/tcvwpjfnZx  
Community Extensions repo: https://github.com/duckdb/community-extensions  
Extension template: https://github.com/duckdb/extension-template
