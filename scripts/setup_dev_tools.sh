#!/bin/bash
set -e

echo "========================================"
echo "CloudFS Development Tools Setup"
echo "========================================"
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
    PKG_MANAGER="apt-get"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
    PKG_MANAGER="brew"
else
    echo -e "${RED}❌ Unsupported OS: $OSTYPE${NC}"
    exit 1
fi

echo "Detected OS: $OS"
echo ""

# ============================================================================
# 1. Install Python (if not installed)
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "1. Checking Python..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v python3 &> /dev/null; then
    PYTHON_VERSION=$(python3 --version)
    echo -e "${GREEN}✓${NC} Python installed: $PYTHON_VERSION"
else
    echo -e "${YELLOW}⚠${NC} Python not found, installing..."
    if [ "$OS" = "linux" ]; then
        sudo apt-get update
        sudo apt-get install -y python3 python3-pip
    else
        brew install python3
    fi
fi

# ============================================================================
# 2. Install Node.js (for commitlint)
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "2. Checking Node.js..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v node &> /dev/null; then
    NODE_VERSION=$(node --version)
    echo -e "${GREEN}✓${NC} Node.js installed: $NODE_VERSION"
else
    echo -e "${YELLOW}⚠${NC} Node.js not found, installing..."
    if [ "$OS" = "linux" ]; then
        curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
        sudo apt-get install -y nodejs
    else
        brew install node
    fi
fi

# ============================================================================
# 3. Install pre-commit
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "3. Installing pre-commit..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v pre-commit &> /dev/null; then
    echo -e "${GREEN}✓${NC} pre-commit already installed"
else
    echo "Installing pre-commit via pip..."
    pip3 install pre-commit
fi

# ============================================================================
# 4. Install commitlint
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "4. Installing commitlint..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -f "package.json" ]; then
    echo "Using existing package.json..."
    npm install
else
    echo "Creating package.json and installing commitlint..."
    npm init -y
    npm install --save-dev @commitlint/cli @commitlint/config-conventional
fi

# ============================================================================
# 5. Install clang-format (if not installed)
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "5. Checking clang-format..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v clang-format &> /dev/null; then
    CLANG_VERSION=$(clang-format --version)
    echo -e "${GREEN}✓${NC} clang-format installed: $CLANG_VERSION"
else
    echo -e "${YELLOW}⚠${NC} clang-format not found, installing..."
    if [ "$OS" = "linux" ]; then
        sudo apt-get install -y clang-format
    else
        brew install clang-format
    fi
fi

# ============================================================================
# 6. Install shellcheck
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "6. Checking shellcheck..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v shellcheck &> /dev/null; then
    SHELLCHECK_VERSION=$(shellcheck --version | head -2 | tail -1)
    echo -e "${GREEN}✓${NC} shellcheck installed: $SHELLCHECK_VERSION"
else
    echo -e "${YELLOW}⚠${NC} shellcheck not found, installing..."
    if [ "$OS" = "linux" ]; then
        sudo apt-get install -y shellcheck
    else
        brew install shellcheck
    fi
fi

# ============================================================================
# 7. Install cmake-format (optional)
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "7. Installing cmake-format (optional)..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v cmake-format &> /dev/null; then
    echo -e "${GREEN}✓${NC} cmake-format already installed"
else
    echo "Installing cmake-format via pip..."
    pip3 install cmake-format
fi

# ============================================================================
# 8. Install pre-commit hooks
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "8. Installing pre-commit hooks..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -d .git ]; then
    pre-commit install --hook-type pre-commit
    pre-commit install --hook-type commit-msg
    echo -e "${GREEN}✓${NC} Pre-commit hooks installed"
else
    echo -e "${YELLOW}⚠${NC} Not a git repository. Run 'git init' first."
fi

# ============================================================================
# 9. Initialize secrets baseline
# ============================================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "9. Initializing secrets detection baseline..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ ! -f ".secrets.baseline" ]; then
    pip3 install detect-secrets
    detect-secrets scan > .secrets.baseline
    echo -e "${GREEN}✓${NC} Secrets baseline created"
else
    echo -e "${GREEN}✓${NC} Secrets baseline already exists"
fi

# ============================================================================
# Done!
# ============================================================================
echo ""
echo "========================================"
echo -e "${GREEN}✓ Setup Complete!${NC}"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Test pre-commit: pre-commit run --all-files"
echo "  2. Make a commit with proper format:"
echo "     git commit -m 'feat(core): add new feature'"
echo ""
echo "Commit message format:"
echo "  <type>(<scope>): <subject>"
echo ""
echo "Valid types:"
echo "  feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert"
echo ""
echo "Valid scopes:"
echo "  core, table-functions, auth, cache, http, providers, onedrive,"
echo "  sharepoint, gdrive, dropbox, sftp, vfs, agent, extension, build,"
echo "  deps, docs, tests, scripts"
echo ""
echo "Examples:"
echo "  fix(table-functions): handle null pointers in ls()"
echo "  feat(onedrive): add delta query support"
echo "  docs(readme): update build instructions"
echo ""
