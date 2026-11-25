# CloudFS - DuckDB Cloud Storage Extension

[![DuckDB](https://img.shields.io/badge/DuckDB-v1.5.4-blue.svg)](https://duckdb.org/)
[![C++17](https://img.shields.io/badge/C++-17-00599C.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

> A DuckDB extension that registers cloud storage protocols as first-class
> filesystems, so Parquet, CSV and JSON files can be queried in place.

## 🚀 Features

- **Multi-Provider Support**: OneDrive, SharePoint, Google Drive, Dropbox, SFTP,
  and a local VFS agent
- **Protocol Integration**: Use cloud URLs directly in DuckDB queries
  (`spfs://`, `odfs://`, `gdfs://`, `dbxfs://`, `sftp://`, `vfs://`)
- **Table Functions**: Built-in `ls()`, `stat()`, `du()` for filesystem
  exploration
- **Metadata Caching**: TTL-based cache for resolved paths, download URLs and
  drive IDs
- **Auth via DuckDB Secrets**: OAuth2 device code, client credentials, service
  account, SSH keys and static tokens
- **Range Reads**: Only the bytes DuckDB asks for are downloaded (Parquet
  footers, projected columns)

## 📦 Quick Start

### Prerequisites

```bash
# Linux/WSL
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
    libssl-dev libcurl4-openssl-dev libssh2-1-dev

# macOS
brew install cmake ninja openssl curl libssh2
```

### Build

```bash
git submodule update --init --recursive
make release
```

This builds DuckDB itself plus the extension. The outputs are:

- `build/release/duckdb` — a DuckDB shell with CloudFS linked in
- `build/release/extension/cloudfs/cloudfs.duckdb_extension` — the loadable
  extension

### Load

The extension is unsigned, so DuckDB must be started with `-unsigned`:

```bash
duckdb -unsigned
```

```sql
LOAD 'build/release/extension/cloudfs/cloudfs.duckdb_extension';
SELECT cloudfs_version();
```

A loadable extension only works with the exact DuckDB version it was built
against (currently v1.5.4).

## 🔧 Usage

### Basic Example

```sql
-- Create a secret for OneDrive (app-only / headless)
-- WARNING: CREATE SECRET statements are stored in CLI history as plain text.
CREATE SECRET onedrive_secret (
    TYPE onedrive,
    PROVIDER client_credentials,
    TENANT_ID 'your-tenant-id',
    CLIENT_ID 'your-client-id',
    CLIENT_SECRET 'your-client-secret'
);

-- List files
SELECT * FROM ls('odfs://me/Documents/');

-- Read Parquet directly
SELECT * FROM 'odfs://me/Data/sales.parquet';

-- File metadata
SELECT name, size, modified FROM stat('odfs://me/report.csv');

-- Directory usage
SELECT * FROM du('odfs://me/Projects/');
```

### Supported Protocols

| Protocol   | Provider     | Example URL                                                 |
| ---------- | ------------ | ----------------------------------------------------------- |
| `odfs://`  | OneDrive     | `odfs://me/Documents/file.parquet`                          |
| `spfs://`  | SharePoint   | `spfs://contoso.sharepoint.com/sites/Team/Library/data.csv` |
| `gdfs://`  | Google Drive | `gdfs://MyDrive/dataset.parquet`                            |
| `dbxfs://` | Dropbox      | `dbxfs:///work/analysis.csv`                                |
| `sftp://`  | SFTP         | `sftp://user@server:22/path/to/file.parquet`                |
| `vfs://`   | VFS Agent    | `vfs://localhost:8765/data/file.csv`                        |

### Secret Types

| `TYPE`       | `PROVIDER`           | Parameters                                          |
| ------------ | -------------------- | --------------------------------------------------- |
| `sharepoint` | `oauth` (default)    | `TENANT_ID`, `CLIENT_ID`, `SCOPE` (all optional)    |
| `sharepoint` | `client_credentials` | `TENANT_ID`, `CLIENT_ID`, `CLIENT_SECRET`, `SCOPE`  |
| `sharepoint` | `token`              | `TOKEN`                                             |
| `onedrive`   | `oauth` (default)    | `TENANT_ID`, `CLIENT_ID` (both optional)            |
| `onedrive`   | `client_credentials` | `TENANT_ID`, `CLIENT_ID`, `CLIENT_SECRET`, `SCOPE`  |
| `onedrive`   | `token`              | `TOKEN`                                             |
| `gdrive`     | `oauth` (default)    | `CLIENT_ID`, `CLIENT_SECRET`                        |
| `gdrive`     | `service_account`    | `KEY_JSON` (contents of the service account key)    |
| `gdrive`     | `token`              | `TOKEN`                                             |
| `dropbox`    | `oauth` (default)    | `APP_KEY`, `APP_SECRET`                             |
| `dropbox`    | `token`              | `TOKEN`                                             |
| `sftp`       | `keyfile` (default)  | `KEY_PATH`, `PASSPHRASE` (optional), `HOST`, `USER` |
| `sftp`       | `agent`              | `HOST`, `USER`                                      |
| `sftp`       | `password`           | `PASSWORD`, `HOST`, `USER`                          |
| `vfs`        | `token`              | `TOKEN`                                             |

The `oauth` providers use the device code flow: the first access prints a URL
and a code on stderr and waits for you to authorize in a browser.

### Utility Functions

```sql
SELECT cloudfs_version();           -- extension version
SELECT cloudfs_providers();         -- registered URL schemes
SELECT cloudfs_clear_cache();       -- drop all cached metadata
SELECT cloudfs_clear_cache('spfs'); -- drop cached metadata for one scheme
```

## ⚠️ Known Limitations

These are tracked in the
[v0.2.0 milestone](https://github.com/trouchet/cloudfs/milestone/1):

- **Secrets are bound at `CREATE SECRET` time.** Persistent secrets are not
  picked up after a restart, `DROP SECRET` does not revoke access, and `SCOPE`
  is ignored: the last secret created for a type wins
  ([#17](https://github.com/trouchet/cloudfs/issues/17)).
- **One database per process.** Loading the extension into a second DuckDB
  database in the same process is not supported
  ([#18](https://github.com/trouchet/cloudfs/issues/18)).
- **Listing errors look like "no files found".** Authentication and network
  failures during glob expansion are not reported as such
  ([#19](https://github.com/trouchet/cloudfs/issues/19)).
- **Special characters in names.** File and folder names containing spaces,
  quotes, `#` or `%` may fail on some providers
  ([#20](https://github.com/trouchet/cloudfs/issues/20)).
- **Writes are buffered in memory.** A file written with `COPY ... TO` is held
  entirely in memory until it is closed
  ([#21](https://github.com/trouchet/cloudfs/issues/21)).

## 📁 Project Structure

```
cloudfs/
├── src/
│   ├── core/              # Filesystem, cache, HTTP client, auth, secrets,
│   │                      # table functions
│   ├── providers/         # One directory per backend
│   │   ├── onedrive/
│   │   ├── sharepoint/
│   │   ├── gdrive/
│   │   ├── dropbox/
│   │   ├── sftp/
│   │   └── vfs/
│   ├── extension/         # DuckDB extension entry point
│   └── include/           # Headers (core/ and providers/)
├── agent/                 # cloudfs-agent: Go HTTP server behind vfs://
├── test/                  # SQL tests and the Python VFS suite
├── docs/                  # Documentation
├── scripts/               # Build, setup and development scripts
├── duckdb/                # DuckDB (git submodule)
└── extension-ci-tools/    # DuckDB extension build tooling (git submodule)
```

### Key Components

- **CloudFileSystem**: the single DuckDB `FileSystem` implementation; dispatches
  by URL scheme
- **ICloudBackend**: provider interface (stateless, capabilities-driven)
- **ICloudAuthProvider**: authentication abstraction (OAuth2, token, key-based)
- **CloudCache**: TTL cache for path → item (15 min), download URLs (50 min) and
  root IDs (24 h)
- **CloudHttpClient**: libcurl wrapper with retry and exponential backoff

See [docs/adding_a_provider.md](docs/adding_a_provider.md) to add a backend.

## 📖 Documentation

**First time?** Start here → [docs/START_HERE.md](docs/START_HERE.md)

- **[Setup Guide](docs/SETUP_GUIDE.md)** - Comprehensive setup instructions
- **[Build Quickstart](docs/BUILD_QUICKSTART.md)** - Detailed build instructions
- **[SharePoint Quickstart](docs/QUICKSTART_SHAREPOINT.md)** - SharePoint
  integration examples
- **[Development Guide](docs/DEVELOPMENT.md)** - Contributing and development
  workflow
- **[Adding a Provider](docs/adding_a_provider.md)** - How to add new cloud
  providers
- **[Commit Guidelines](docs/COMMIT_PLAN.md)** - Git commit conventions
- **[How to Contribute](docs/CONTRIBUTING_PT.md)** - Contributing guidelines
  (Portuguese)
- **[Submit Patches](docs/COMO_SUBMETER_PATCHES.md)** - Quick guide to
  submitting patches (Portuguese)
- **[Changelog](CHANGELOG.md)** - Release notes

## 🛠️ Development

### Setup

```bash
scripts/setup_dev_tools.sh     # commitlint, pre-commit, clang-format
scripts/validate_dev_setup.sh  # check the setup
```

### Common Tasks

```bash
make release        # Build (optimized)
make debug          # Build (debug)
make test           # Run the SQL test suite against the release build
make format         # Format code
make format-check   # Check formatting without changing files
make clean          # Remove build output
```

### Commit Guidelines

We use [Conventional Commits](https://www.conventionalcommits.org/):

```bash
# Format: <type>(<scope>): <subject>
git commit -m "feat(gdrive): add resumable uploads"
git commit -m "fix(table-functions): handle null pointers"
git commit -m "docs(readme): update installation steps"
```

Valid types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`,
`build`, `ci`, `chore`, `revert`

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for complete guidelines.

## 🧪 Testing

```bash
# SQL tests (no credentials needed)
make test

# VFS round-trip suite (needs the built extension and cloudfs-agent)
python3 test/test_suite.py
```

To try the VFS backend by hand, build and start the agent, then point DuckDB at
it:

```bash
(cd agent && go build -o ../cloudfs-agent .)
./cloudfs-agent --token your-agent-token --port 8765 --root /data
```

```sql
CREATE SECRET v (TYPE vfs, PROVIDER token, TOKEN 'your-agent-token');
SELECT * FROM ls('vfs://localhost:8765/');
```

## 🤝 Contributing

1. Fork the repository
1. Create a feature branch: `git checkout -b feat/amazing-feature`
1. Make your changes following our [commit guidelines](docs/COMMIT_PLAN.md)
1. Run `make format-check` and `make test`
1. Commit: `git commit -m "feat(provider): add amazing feature"`
1. Push: `git push origin feat/amazing-feature`
1. Open a Pull Request

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for detailed contributing
guidelines.

## 📋 Requirements

- **C++ Compiler**: GCC or Clang with C++17 support
- **CMake**: 3.10 or later
- **OpenSSL**, **libcurl**, **libssh2** (for SFTP), **zlib**
- **DuckDB**: v1.5.4 (built from the `duckdb` submodule)
- **Go**: only to build `cloudfs-agent`

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- [DuckDB](https://duckdb.org/) - Amazing embedded database
- Built on top of DuckDB's extension template

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/trouchet/cloudfs/issues)

______________________________________________________________________

**Made with ❤️ for the DuckDB community**
