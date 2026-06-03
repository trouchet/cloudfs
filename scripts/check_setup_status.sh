#!/bin/bash
# Visual setup progress tracker

BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

clear

echo -e "${BOLD}${BLUE}"
cat << "EOF"
╔═══════════════════════════════════════════════════════════════╗
║                                                               ║
║           CloudFS Development Environment Setup              ║
║                                                               ║
╚═══════════════════════════════════════════════════════════════╝
EOF
echo -e "${NC}"

check_status() {
    if [ "$1" = "true" ]; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗${NC}"
    fi
}

# Check Python
echo -n "Python 3           "
PYTHON_OK="false"
if command -v python3 &> /dev/null; then
    PYTHON_OK="true"
fi
check_status "$PYTHON_OK"

# Check Node.js
echo -n "Node.js            "
NODE_OK="false"
if command -v node &> /dev/null; then
    NODE_OK="true"
fi
check_status "$NODE_OK"

# Check npm
echo -n "npm                "
NPM_OK="false"
if command -v npm &> /dev/null; then
    NPM_OK="true"
fi
check_status "$NPM_OK"

# Check pre-commit
echo -n "pre-commit         "
PRECOMMIT_OK="false"
if command -v pre-commit &> /dev/null; then
    PRECOMMIT_OK="true"
fi
check_status "$PRECOMMIT_OK"

# Check commitlint
echo -n "commitlint         "
COMMITLINT_OK="false"
if [ -d "node_modules/@commitlint" ] || npx commitlint --version &> /dev/null; then
    COMMITLINT_OK="true"
fi
check_status "$COMMITLINT_OK"

# Check clang-format
echo -n "clang-format       "
CLANGFMT_OK="false"
if command -v clang-format &> /dev/null; then
    CLANGFMT_OK="true"
fi
check_status "$CLANGFMT_OK"

# Check shellcheck
echo -n "shellcheck         "
SHELLCHECK_OK="false"
if command -v shellcheck &> /dev/null; then
    SHELLCHECK_OK="true"
fi
check_status "$SHELLCHECK_OK"

# Check cmake-format
echo -n "cmake-format       "
CMAKEFMT_OK="false"
if command -v cmake-format &> /dev/null; then
    CMAKEFMT_OK="true"
fi
check_status "$CMAKEFMT_OK"

# Check Git hooks
echo -n "Git hooks          "
HOOKS_OK="false"
if [ -f ".git/hooks/pre-commit" ] && [ -f ".git/hooks/commit-msg" ]; then
    HOOKS_OK="true"
fi
check_status "$HOOKS_OK"

# Check configuration files
echo ""
echo -e "${BOLD}Configuration Files:${NC}"

echo -n ".commitlintrc.json        "
FILE1="false"
if [ -f ".commitlintrc.json" ]; then
    FILE1="true"
fi
check_status "$FILE1"

echo -n ".pre-commit-config.yaml   "
FILE2="false"
if [ -f ".pre-commit-config.yaml" ]; then
    FILE2="true"
fi
check_status "$FILE2"

echo -n ".clang-format             "
FILE3="false"
if [ -f ".clang-format" ]; then
    FILE3="true"
fi
check_status "$FILE3"

echo -n ".editorconfig             "
FILE4="false"
if [ -f ".editorconfig" ]; then
    FILE4="true"
fi
check_status "$FILE4"

echo -n "package.json              "
FILE5="false"
if [ -f "package.json" ]; then
    FILE5="true"
fi
check_status "$FILE5"

echo -n ".secrets.baseline         "
FILE6="false"
if [ -f ".secrets.baseline" ]; then
    FILE6="true"
fi
check_status "$FILE6"

echo ""
echo -e "${BOLD}Progress:${NC}"

# Count completed
TOTAL=15
COMPLETED=0

[ "$PYTHON_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$NODE_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$NPM_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$PRECOMMIT_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$COMMITLINT_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$CLANGFMT_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$SHELLCHECK_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$CMAKEFMT_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$HOOKS_OK" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$FILE1" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$FILE2" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$FILE3" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$FILE4" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$FILE5" = "true" ] && COMPLETED=$((COMPLETED + 1))
[ "$FILE6" = "true" ] && COMPLETED=$((COMPLETED + 1))

PERCENT=$((COMPLETED * 100 / TOTAL))

# Progress bar
BAR_WIDTH=50
FILLED=$((COMPLETED * BAR_WIDTH / TOTAL))
EMPTY=$((BAR_WIDTH - FILLED))

echo -n "["
for ((i=0; i<FILLED; i++)); do echo -n "█"; done
for ((i=0; i<EMPTY; i++)); do echo -n "░"; done
echo "] $PERCENT% ($COMPLETED/$TOTAL)"

echo ""

if [ $COMPLETED -eq $TOTAL ]; then
    echo -e "${GREEN}${BOLD}✓ Setup Complete!${NC}"
    echo ""
    echo "You can now:"
    echo "  • make format       - Format your code"
    echo "  • make lint         - Run linters"
    echo "  • make build        - Build the project"
    echo "  • git commit        - Commit with validation"
    echo ""
elif [ $COMPLETED -ge 9 ]; then
    echo -e "${YELLOW}${BOLD}⚠ Almost there!${NC}"
    echo ""
    echo "Missing items are optional but recommended."
    echo "Run: ${BOLD}./setup_dev_tools.sh${NC}"
    echo ""
else
    echo -e "${RED}${BOLD}✗ Setup Incomplete${NC}"
    echo ""
    echo "Please run: ${BOLD}./setup_dev_tools.sh${NC}"
    echo "Or manually: ${BOLD}make dev-setup${NC}"
    echo ""
fi
