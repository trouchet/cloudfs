# CloudFS - DuckDB Cloud Storage Extension

[![DuckDB](https://img.shields.io/badge/DuckDB-v1.4.0+-blue.svg)](https://duckdb.org/)
[![C++17](https://img.shields.io/badge/C++-17-00599C.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

> A DuckDB extension that registers cloud storage protocols as first-class filesystems with native support for Parquet, CSV, Delta Lake, and Iceberg.

## 🚀 Features

- **Multi-Provider Support**: OneDrive, SharePoint, Google Drive, Dropbox, SFTP, and local VFS
- **Protocol Integration**: Use cloud URLs directly in DuckDB queries (`spfs://`, `odfs://`, `gdfs://`, `dbxfs://`, `sftp://`, `vfs://`)
- **Table Functions**: Built-in `ls()`, `stat()`, `du()` for filesystem exploration
- **Smart Caching**: 3-tier LRU cache with TTL for optimal performance
- **Secure Auth**: OAuth2 flows with token persistence via DuckDB secrets
- **Format Support**: Parquet, CSV, JSON, Delta Lake, Iceberg

## 📦 Quick Start

### Prerequisites

```bash
# Linux/WSL
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
    libssl-dev libcurl4-openssl-dev libssh2-1-dev

# macOS
brew install cmake ninja openssl libcurl libssh2
```

### Build

```bash
# Quick build
make build

# Or manually
mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
ninja
```

### Install

```bash
# Copy extension to DuckDB extensions directory
cp build/cloudfs.duckdb_extension ~/.duckdb/extensions/v1.4.0/linux_amd64/
```

## 🔧 Usage

### Basic Example

```sql
-- Load extension
LOAD cloudfs;

-- Create secret for OneDrive
CREATE SECRET onedrive_secret (
    TYPE onedrive,
    PROVIDER config,
    CLIENT_ID 'your-client-id',
    CLIENT_SECRET 'your-client-secret'
);

-- List files
SELECT * FROM ls('odfs://Documents/');

-- Read Parquet directly
SELECT * FROM 'odfs://Data/sales.parquet';

-- File metadata
SELECT name, size, modified FROM stat('odfs://report.csv');

-- Directory usage
SELECT * FROM du('odfs://Projects/');
```

### Supported Protocols

| Protocol | Provider | Example URL |
|----------|----------|-------------|
| `odfs://` | OneDrive | `odfs://Documents/file.parquet` |
| `spfs://` | SharePoint | `spfs://sites/team/Shared Documents/data.csv` |
| `gdfs://` | Google Drive | `gdfs://My Drive/dataset.parquet` |
| `dbxfs://` | Dropbox | `dbxfs:///work/analysis.csv` |
| `sftp://` | SFTP | `sftp://user@server/path/to/file.parquet` |
| `vfs://` | VFS Agent | `vfs://localhost:19876/data/file.csv` |

## 📖 Documentation

- **[Development Guide](docs/DEVELOPMENT.md)** - Contributing and development workflow
- **[Build Quickstart](docs/BUILD_QUICKSTART.md)** - Detailed build instructions
- **[Commit Guidelines](docs/COMMIT_PLAN.md)** - Git commit conventions
- **[Adding a Provider](docs/adding_a_provider.md)** - How to add new cloud providers
- **[Task Status](docs/TASK1_STATUS.md)** - Current development status

## 🛠️ Development

### Setup Development Environment

```bash
# Install all dev tools (commitlint, pre-commit, clang-format, etc.)
make dev-setup

# Validate setup
make validate
```

### Common Tasks

```bash
make help           # Show all available commands
make format         # Format code (C++, CMake, Shell)
make lint           # Run all linters
make test           # Run tests
make check-all      # Run all checks
```

### Commit Guidelines

We use [Conventional Commits](https://www.conventionalcommits.org/):

```bash
# Format: <type>(<scope>): <subject>
git commit -m "feat(gdrive): add resumable uploads"
git commit -m "fix(table-functions): handle null pointers"
git commit -m "docs(readme): update installation steps"
```

Valid types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for complete guidelines.

## 🏗️ Architecture

```
cloudfs/
├── src/
│   ├── core/              # Core filesystem abstraction
│   │   ├── cloud_filesystem.cpp
│   │   ├── cloud_cache.cpp
│   │   ├── cloud_http.cpp
│   │   ├── cloud_auth.cpp
│   │   └── cloud_table_functions.cpp
│   ├── providers/         # Cloud provider implementations
│   │   ├── onedrive/
│   │   ├── sharepoint/
│   │   ├── gdrive/
│   │   ├── dropbox/
│   │   ├── sftp/
│   │   └── vfs/
│   └── extension/         # DuckDB extension wrapper
├── agent/                 # VFS Go agent service
├── test/                  # SQL tests
├── docs/                  # Documentation
└── scripts/               # Build and development scripts
```

### Key Components

- **CloudFileSystem**: DuckDB `FileSystem` implementation
- **ICloudBackend**: Provider interface (stateless, capabilities-driven)
- **ICloudAuthProvider**: Authentication abstraction (OAuth2, token, config)
- **CloudCache**: 3-tier LRU cache (metadata, content, partial)
- **CloudHttpClient**: libcurl wrapper with retry logic

## 🧪 Testing

```bash
# Run full test suite
make test

# Quick validation
scripts/check_deps.sh       # Check dependencies
scripts/build_and_test.sh   # Build and test

# Test table functions manually
duckdb -unsigned << EOF
LOAD './cloudfs.duckdb_extension';
SELECT cloudfs_version();
SELECT * FROM ls('vfs://localhost:19876/');
EOF
```

## 📊 Performance

- **Metadata Caching**: TTL-based with configurable expiration
- **Content Caching**: LRU with size limits
- **Partial Downloads**: Range requests for selective reads
- **Batch Operations**: Optimized for Delta queries (OneDrive/SharePoint)

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feat/amazing-feature`
3. Make your changes following our [commit guidelines](docs/COMMIT_PLAN.md)
4. Ensure all checks pass: `make check-all`
5. Commit: `git commit -m "feat(provider): add amazing feature"`
6. Push: `git push origin feat/amazing-feature`
7. Open a Pull Request

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for detailed contributing guidelines.

## 📋 Requirements

- **C++ Compiler**: GCC 7+ or Clang 6+ (C++17 support)
- **CMake**: 3.5 or later
- **Ninja**: Build system
- **OpenSSL**: 1.1.0 or later
- **libcurl**: 7.58.0 or later
- **libssh2**: 1.8.0 or later (for SFTP)
- **DuckDB**: 1.4.0 or later

## 🐛 Known Issues & Roadmap

See [AGENT_HANDOVER.md](AGENT_HANDOVER.md) for current status and next steps.

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- [DuckDB](https://duckdb.org/) - Amazing embedded database
- Built on top of DuckDB's extension template

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/yourusername/cloudfs/issues)
- **Discussions**: [GitHub Discussions](https://github.com/yourusername/cloudfs/discussions)

---

**Made with ❤️ for the DuckDB community**
