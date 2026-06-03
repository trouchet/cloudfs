# 🔧 CloudFS Scripts

This directory contains all build, test, and development automation scripts for
the CloudFS project.

## 📋 Available Scripts

### Setup & Validation

#### `setup_dev_tools.sh`

**Purpose**: Install all development tools\
**Usage**:
`./scripts/setup_dev_tools.sh`\
**Installs**:

- Python 3, Node.js, npm
- pre-commit, commitlint
- clang-format, shellcheck, cmake-format
- detect-secrets

**When to use**: First time setup or after clean system install

______________________________________________________________________

#### `validate_dev_setup.sh`

**Purpose**: Validate development environment\
**Usage**:
`./scripts/validate_dev_setup.sh` or `make validate`\
**Checks**:

- All dev tools installed
- Git hooks configured
- Configuration files present
- Recent commits follow conventions

**When to use**: After setup or before starting work

______________________________________________________________________

#### `check_setup_status.sh`

**Purpose**: Visual progress dashboard\
**Usage**:
`./scripts/check_setup_status.sh`\
**Shows**:

- Progress bar of setup completion
- Tool installation status
- Configuration file status

**When to use**: Quick visual check of environment

______________________________________________________________________

### Build & Test

#### `build_and_test.sh`

**Purpose**: Full build and test pipeline\
**Usage**:
`./scripts/build_and_test.sh` or `make build`\
**Does**:

- Checks dependencies
- Cleans old build
- Runs CMake + Ninja
- Starts VFS agent
- Tests all table functions (ls, stat, du)
- Validates file reading

**When to use**: After code changes or for full validation

______________________________________________________________________

#### `check_deps.sh`

**Purpose**: Verify build dependencies\
**Usage**:
`./scripts/check_deps.sh`\
**Checks**:

- cmake, ninja, compilers
- libssl-dev, libcurl, libssh2
- DuckDB installation
- Project structure

**When to use**: Before building, troubleshooting build issues

______________________________________________________________________

### Commit & Quality

#### `test_commitlint.sh`

**Purpose**: Test commit message validation\
**Usage**:
`./scripts/test_commitlint.sh`\
**Tests**:

- Valid commit messages (should pass)
- Invalid commit messages (should fail)
- All commit rules

**When to use**: After commitlint configuration changes

______________________________________________________________________

#### `execute_commit_plan.sh`

**Purpose**: Execute structured commit plan\
**Usage**:
`./scripts/execute_commit_plan.sh`\
**Does**:

- Creates multiple atomic commits
- Follows conventional commit format
- Organizes changes logically

**When to use**: When applying the commit plan from docs/COMMIT_PLAN.md

______________________________________________________________________

## 🚀 Quick Reference

### First Time Setup

```bash
# 1. Install tools
./scripts/setup_dev_tools.sh

# 2. Validate
./scripts/validate_dev_setup.sh

# 3. Check status
./scripts/check_setup_status.sh
```

### Daily Development

```bash
# Check dependencies
./scripts/check_deps.sh

# Build and test
./scripts/build_and_test.sh

# Or use make
make build
make test
```

### Before Committing

```bash
# Format code
make format

# Run linters
make lint

# Validate environment
./scripts/validate_dev_setup.sh

# Test commitlint rules
./scripts/test_commitlint.sh
```

______________________________________________________________________

## 🔧 Using with Make

All scripts can be invoked via Makefile targets:

| Script                  | Make Command      | Description             |
| ----------------------- | ----------------- | ----------------------- |
| `setup_dev_tools.sh`    | `make setup`      | Install dev tools       |
| `validate_dev_setup.sh` | `make validate`   | Validate environment    |
| `build_and_test.sh`     | `make build`      | Build extension         |
| `check_deps.sh`         | (in build script) | Check dependencies      |
| Combined                | `make dev-setup`  | Full setup + validation |

See `make help` for all available commands.

______________________________________________________________________

## 📝 Script Details

### setup_dev_tools.sh

**What it does**:

1. Detects OS (Linux/macOS)
1. Installs Python 3 (if missing)
1. Installs Node.js (if missing)
1. Installs pre-commit via pip
1. Installs commitlint via npm
1. Installs clang-format, shellcheck, cmake-format
1. Installs Git hooks
1. Creates secrets baseline

**Exit codes**:

- `0` - Success
- `1` - Installation failed

______________________________________________________________________

### validate_dev_setup.sh

**What it checks**:

1. Development tools (pre-commit, node, clang-format, shellcheck)
1. Git hooks installation
1. Configuration files (.commitlintrc.json, .pre-commit-config.yaml, etc.)
1. Recent commit messages
1. Code formatting
1. Shell script linting
1. Secrets detection

**Exit codes**:

- `0` - All checks passed
- `1` - Errors found

______________________________________________________________________

### build_and_test.sh

**Build process**:

1. Checks dependencies (aborts if missing)
1. Removes old build directory
1. Creates new build directory
1. Runs CMake with Ninja generator
1. Runs Ninja build
1. Copies extension to project root

**Test process**:

1. Generates random token for VFS agent
1. Creates test data in /tmp
1. Starts cloudfs-agent
1. Tests: version, providers, secrets, ls(), stat(), du(), file reading
1. Cleans up agent and test data

**Exit codes**:

- `0` - Build and tests successful
- `1` - Dependencies missing or build/test failed

______________________________________________________________________

### check_deps.sh

**Dependencies checked**:

- cmake (required)
- ninja (required)
- libssl-dev (required)
- libcurl4-openssl-dev (required)
- libssh2-1-dev (required)
- duckdb (required)

**Output**:

- Lists all dependencies with status
- Shows DuckDB version
- Provides install commands for missing deps

**Exit codes**:

- `0` - All dependencies present
- `1` - Missing dependencies

______________________________________________________________________

## 🐛 Troubleshooting

### Script won't run

```bash
# Make executable
chmod +x scripts/*.sh

# Or run with bash
bash scripts/setup_dev_tools.sh
```

### Permission denied on sudo

```bash
# Enable sudo in Windows
# Settings → Developer Settings → Enable sudo
```

### Node.js not found after install

```bash
# Reload shell
source ~/.bashrc
# or
exec $SHELL
```

### pre-commit hooks not running

```bash
# Reinstall hooks
pre-commit install --hook-type pre-commit
pre-commit install --hook-type commit-msg
```

______________________________________________________________________

## 🔄 Script Maintenance

### Adding a new script

1. Create script in `scripts/` directory
1. Make it executable: `chmod +x scripts/new_script.sh`
1. Add shebang: `#!/bin/bash`
1. Add to this README
1. Optionally add to Makefile

### Updating scripts

1. Edit the script
1. Test manually
1. Update documentation if behavior changes
1. Commit: `git commit -m "chore(scripts): update script_name"`

______________________________________________________________________

## 📚 Related Documentation

- **[Development Guide](../docs/DEVELOPMENT.md)** - Full development workflow
- **[Build Quickstart](../docs/BUILD_QUICKSTART.md)** - Build instructions
- **[Commit Plan](../docs/COMMIT_PLAN.md)** - Commit strategy

______________________________________________________________________

**All scripts follow shellcheck best practices and are tested on Linux/WSL and
macOS.**
