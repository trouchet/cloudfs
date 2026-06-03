# 🛠️ CloudFS Development Guide

## 📋 Table of Contents

- [Setup](#setup)
- [Commit Guidelines](#commit-guidelines)
- [Code Style](#code-style)
- [Pre-commit Hooks](#pre-commit-hooks)
- [Testing](#testing)
- [Common Tasks](#common-tasks)

---

## 🚀 Setup

### 1. Install Development Tools

```bash
chmod +x setup_dev_tools.sh
./setup_dev_tools.sh
```

This script installs:
- ✅ Python 3 + pip
- ✅ Node.js + npm
- ✅ pre-commit
- ✅ commitlint
- ✅ clang-format
- ✅ shellcheck
- ✅ cmake-format
- ✅ detect-secrets

### 2. Verify Installation

```bash
# Check pre-commit hooks
pre-commit --version

# Run all checks on existing files
pre-commit run --all-files

# Test commit message validation
echo "test: invalid commit" | npx commitlint
```

---

## 📝 Commit Guidelines

### Format

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

### Valid Types

| Type | Description | Example |
|------|-------------|---------|
| `feat` | New feature | `feat(gdrive): add resumable uploads` |
| `fix` | Bug fix | `fix(table-functions): handle null pointers` |
| `docs` | Documentation only | `docs(readme): update build instructions` |
| `style` | Code style changes | `style(core): fix indentation` |
| `refactor` | Code refactoring | `refactor(cache): simplify LRU logic` |
| `perf` | Performance improvements | `perf(http): reduce connection overhead` |
| `test` | Add or update tests | `test(providers): add OneDrive auth tests` |
| `build` | Build system changes | `build(cmake): update DuckDB version` |
| `ci` | CI configuration | `ci(github): add clang-tidy workflow` |
| `chore` | Maintenance tasks | `chore(deps): update vcpkg packages` |
| `revert` | Revert previous commit | `revert: "feat(gdrive): add batch API"` |

### Valid Scopes

- `core` - Core filesystem/backend
- `table-functions` - ls, stat, du functions
- `auth` - Authentication providers
- `cache` - Caching layer
- `http` - HTTP client
- `providers` - Cloud providers (generic)
- `onedrive` - OneDrive provider
- `sharepoint` - SharePoint provider
- `gdrive` - Google Drive provider
- `dropbox` - Dropbox provider
- `sftp` - SFTP provider
- `vfs` - VFS/agent provider
- `agent` - Go agent service
- `extension` - DuckDB extension wrapper
- `build` - Build system
- `deps` - Dependencies
- `docs` - Documentation
- `tests` - Test infrastructure
- `scripts` - Shell scripts

### Rules

✅ **DO**:
- Use lowercase for type and scope
- Use imperative mood ("add" not "added")
- Keep subject line under 100 characters
- Separate subject from body with blank line
- Wrap body at 100 characters

❌ **DON'T**:
- End subject line with period
- Use past tense
- Include issue numbers in subject (put in footer)
- Capitalize subject line
- Commit directly to master/main branch

### Examples

#### Simple Commit
```bash
git commit -m "fix(table-functions): handle empty directory paths"
```

#### Commit with Body
```bash
git commit -m "feat(onedrive): add delta sync support

Implement delta query API to efficiently sync only changed files.
This reduces bandwidth and improves performance for large datasets.

Closes #123"
```

#### Breaking Change
```bash
git commit -m "feat(auth)!: change OAuth flow to PKCE

BREAKING CHANGE: OAuth tokens now use PKCE flow. Existing tokens
will need to be regenerated."
```

---

## 🎨 Code Style

### C++ Style

We use **clang-format** with LLVM-based style:
- Indent: 4 spaces
- Line length: 100 characters
- Pointer alignment: left (`int* ptr`)
- Braces: attached style

**Format all C++ files**:
```bash
npm run format:cpp
```

**Format single file**:
```bash
clang-format -i src/core/cloud_filesystem.cpp
```

### CMake Style

- Indent: 2 spaces
- Commands: lowercase

**Format CMakeLists.txt**:
```bash
npm run format:cmake
```

### Shell Scripts

- Indent: 2 spaces
- Use shellcheck for linting

**Check shell scripts**:
```bash
npm run check:shell
```

---

## 🪝 Pre-commit Hooks

### What Gets Checked

#### On Every Commit:
- ✅ File size (max 1MB)
- ✅ End of file newlines
- ✅ Trailing whitespace
- ✅ Merge conflict markers
- ✅ YAML/JSON syntax
- ✅ C++ formatting (clang-format)
- ✅ CMake formatting
- ✅ Shell script linting (shellcheck)
- ✅ Markdown formatting
- ✅ Secret detection
- ✅ Go formatting (for agent/)

#### On Commit Message:
- ✅ Conventional commit format
- ✅ Valid type and scope
- ✅ Length limits

### Manual Run

```bash
# Run all hooks on all files
pre-commit run --all-files

# Run specific hook
pre-commit run clang-format --all-files
pre-commit run shellcheck --all-files

# Skip hooks (use sparingly!)
git commit --no-verify -m "wip: temporary commit"
```

### Update Hooks

```bash
# Update to latest versions
pre-commit autoupdate

# Reinstall hooks
pre-commit clean
pre-commit install --hook-type pre-commit
pre-commit install --hook-type commit-msg
```

---

## 🧪 Testing

### Run Tests

```bash
# Full build and test
./build_and_test.sh

# Quick dependency check
./check_deps.sh

# Manual test
duckdb -unsigned << EOF
LOAD './cloudfs.duckdb_extension';
SELECT cloudfs_version();
EOF
```

### Test Before Commit

```bash
# 1. Check code style
npm run lint

# 2. Run unit tests
./build_and_test.sh

# 3. Verify commit message format
echo "fix(core): your message" | npx commitlint
```

---

## 🔧 Common Tasks

### Add a New Provider

1. Create provider files:
   ```bash
   touch src/providers/newprovider/newprovider_backend.cpp
   touch src/include/providers/newprovider_backend.hpp
   ```

2. Format code:
   ```bash
   clang-format -i src/providers/newprovider/*
   ```

3. Commit:
   ```bash
   git add src/providers/newprovider/
   git commit -m "feat(providers): add NewProvider backend"
   ```

### Fix Formatting Issues

```bash
# Let pre-commit fix what it can
pre-commit run --all-files

# Check what changed
git diff

# Commit fixes
git add -u
git commit -m "style: apply clang-format to all files"
```

### Update Dependencies

```bash
# Update vcpkg packages
vcpkg update

# Update npm packages
npm update

# Commit changes
git add vcpkg.json package.json package-lock.json
git commit -m "chore(deps): update dependencies"
```

### Debug Pre-commit Issues

```bash
# Verbose output
pre-commit run --all-files --verbose

# Show which files are checked
pre-commit run --all-files --show-diff-on-failure

# Skip problematic hook temporarily
SKIP=clang-format git commit -m "fix: emergency fix"
```

---

## 🚨 Troubleshooting

### "commitlint not found"

```bash
npm install
npx commitlint --version
```

### "clang-format not found"

```bash
# Linux
sudo apt-get install clang-format

# macOS
brew install clang-format
```

### "pre-commit hook failed"

1. Check what failed:
   ```bash
   pre-commit run --all-files
   ```

2. Auto-fix formatting:
   ```bash
   npm run format:cpp
   git add -u
   ```

3. If still failing, check specific file:
   ```bash
   clang-format --dry-run --Werror src/path/to/file.cpp
   ```

### "Secrets detected"

If detect-secrets flags a false positive:

```bash
# Update baseline to ignore it
detect-secrets scan --baseline .secrets.baseline

# Or add inline pragma to the file:
# pragma: allowlist secret
```

---

## 📚 Additional Resources

- [Conventional Commits](https://www.conventionalcommits.org/)
- [pre-commit Documentation](https://pre-commit.com/)
- [commitlint Rules](https://commitlint.js.org/#/reference-rules)
- [clang-format Style Options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [DuckDB Extension Development](https://duckdb.org/docs/extensions/overview)

---

## ✅ Checklist for Contributors

Before submitting a PR:

- [ ] Code follows project style (run `npm run format:cpp`)
- [ ] All pre-commit hooks pass (`pre-commit run --all-files`)
- [ ] Commit messages follow conventional format
- [ ] Tests pass (`./build_and_test.sh`)
- [ ] Documentation updated (if needed)
- [ ] No secrets committed (`detect-secrets scan`)
- [ ] Branch is up to date with main/master

---

**Happy coding! 🚀**
