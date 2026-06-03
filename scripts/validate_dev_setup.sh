#!/bin/bash
# Quick validation script for development workflow

set -e

BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}   CloudFS Development Validation${NC}"
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

ERRORS=0

# ============================================================================
# Check if development tools are installed
# ============================================================================
echo -e "${BOLD}1. Checking development tools...${NC}"

if command -v pre-commit &> /dev/null; then
    echo -e "${GREEN}✓${NC} pre-commit installed"
else
    echo -e "${RED}✗${NC} pre-commit not found. Run: scripts/setup_dev_tools.sh"
    ERRORS=$((ERRORS + 1))
fi

if command -v node &> /dev/null; then
    echo -e "${GREEN}✓${NC} Node.js installed"
else
    echo -e "${RED}✗${NC} Node.js not found. Run: ./setup_dev_tools.sh"
    ERRORS=$((ERRORS + 1))
fi

if command -v clang-format &> /dev/null; then
    echo -e "${GREEN}✓${NC} clang-format installed"
else
    echo -e "${YELLOW}⚠${NC} clang-format not found (optional)"
fi

if command -v shellcheck &> /dev/null; then
    echo -e "${GREEN}✓${NC} shellcheck installed"
else
    echo -e "${YELLOW}⚠${NC} shellcheck not found (optional)"
fi

# ============================================================================
# Check if Git hooks are installed
# ============================================================================
echo ""
echo -e "${BOLD}2. Checking Git hooks...${NC}"

if [ -f .git/hooks/pre-commit ] && [ -f .git/hooks/commit-msg ]; then
    echo -e "${GREEN}✓${NC} Git hooks installed"
else
    echo -e "${YELLOW}⚠${NC} Git hooks not installed. Run: pre-commit install"
    if [ -d .git ]; then
        echo "  Installing now..."
        pre-commit install --hook-type pre-commit
        pre-commit install --hook-type commit-msg
        echo -e "${GREEN}✓${NC} Hooks installed successfully"
    fi
fi

# ============================================================================
# Check configuration files
# ============================================================================
echo ""
echo -e "${BOLD}3. Checking configuration files...${NC}"

REQUIRED_FILES=(
    ".commitlintrc.json"
    "commitlint.config.js"
    ".pre-commit-config.yaml"
    ".clang-format"
    ".editorconfig"
    ".gitignore"
    "package.json"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo -e "${GREEN}✓${NC} $file"
    else
        echo -e "${RED}✗${NC} $file missing"
        ERRORS=$((ERRORS + 1))
    fi
done

# ============================================================================
# Validate commit message format (if there are commits)
# ============================================================================
echo ""
echo -e "${BOLD}4. Validating recent commits...${NC}"

if git rev-parse HEAD &> /dev/null; then
    LAST_COMMIT=$(git log -1 --pretty=%B 2>/dev/null)

    if echo "$LAST_COMMIT" | npx commitlint &> /dev/null; then
        echo -e "${GREEN}✓${NC} Last commit message is valid"
    else
        echo -e "${YELLOW}⚠${NC} Last commit message doesn't follow conventions"
        echo "  Message: ${LAST_COMMIT:0:50}..."
    fi
else
    echo -e "${YELLOW}⚠${NC} No commits yet"
fi

# ============================================================================
# Check code formatting
# ============================================================================
echo ""
echo -e "${BOLD}5. Checking code formatting...${NC}"

if command -v clang-format &> /dev/null; then
    # Find C++ files that need formatting
    UNFORMATTED=$(find src -name '*.cpp' -o -name '*.hpp' 2>/dev/null | \
                  xargs clang-format --dry-run --Werror 2>&1 || true)

    if [ -z "$UNFORMATTED" ]; then
        echo -e "${GREEN}✓${NC} All C++ files are properly formatted"
    else
        echo -e "${YELLOW}⚠${NC} Some C++ files need formatting"
        echo "  Run: npm run format:cpp"
    fi
else
    echo -e "${YELLOW}⚠${NC} clang-format not available, skipping"
fi

# ============================================================================
# Check shell scripts
# ============================================================================
echo ""
echo -e "${BOLD}6. Checking shell scripts...${NC}"

if command -v shellcheck &> /dev/null; then
    SHELL_ERRORS=$(shellcheck *.sh 2>&1 || true)

    if [ -z "$SHELL_ERRORS" ]; then
        echo -e "${GREEN}✓${NC} All shell scripts pass shellcheck"
    else
        echo -e "${YELLOW}⚠${NC} Some shell scripts have warnings"
        echo "  Run: npm run check:shell"
    fi
else
    echo -e "${YELLOW}⚠${NC} shellcheck not available, skipping"
fi

# ============================================================================
# Check for secrets
# ============================================================================
echo ""
echo -e "${BOLD}7. Checking for secrets...${NC}"

if [ -f ".secrets.baseline" ]; then
    echo -e "${GREEN}✓${NC} Secrets baseline exists"
else
    echo -e "${YELLOW}⚠${NC} Secrets baseline not found"
    if command -v detect-secrets &> /dev/null; then
        echo "  Creating baseline..."
        detect-secrets scan > .secrets.baseline
        echo -e "${GREEN}✓${NC} Baseline created"
    fi
fi

# ============================================================================
# Test commit message validation
# ============================================================================
echo ""
echo -e "${BOLD}8. Testing commit message validation...${NC}"

TEST_MESSAGES=(
    "fix(core): valid commit message"
    "invalid commit message"
    "feat: missing scope"
)

for msg in "${TEST_MESSAGES[@]}"; do
    if echo "$msg" | npx commitlint &> /dev/null; then
        echo -e "${GREEN}✓${NC} '$msg' - PASS"
    else
        echo -e "${RED}✗${NC} '$msg' - FAIL (expected)"
    fi
done

# ============================================================================
# Summary
# ============================================================================
echo ""
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}${BOLD}✓ All checks passed!${NC}"
    echo ""
    echo "You're ready to contribute. Try making a commit:"
    echo "  git add ."
    echo "  git commit -m 'feat(core): your awesome feature'"
    echo ""
    EXIT_CODE=0
else
    echo -e "${RED}${BOLD}✗ $ERRORS error(s) found${NC}"
    echo ""
    echo "Please fix the errors above. Run:"
    echo "  ./setup_dev_tools.sh"
    echo ""
    EXIT_CODE=1
fi

echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

exit $EXIT_CODE
