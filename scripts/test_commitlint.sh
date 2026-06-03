#!/bin/bash
# Test script to validate commitlint rules

set -e

BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}   Commitlint Rules Validation${NC}"
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

if ! command -v npx &> /dev/null; then
    echo -e "${RED}✗${NC} npx not found. Please run: ./setup_dev_tools.sh"
    exit 1
fi

# Test valid commit messages
echo -e "${BOLD}Testing VALID commit messages:${NC}"
echo ""

VALID_MESSAGES=(
    "feat(core): add new feature"
    "fix(table-functions): handle null pointer"
    "docs(readme): update installation"
    "chore(deps): update packages"
    "refactor(cache): simplify LRU logic"
    "perf(http): reduce latency"
    "test(providers): add unit tests"
    "style(core): fix indentation"
    "build(cmake): update DuckDB version"
    "ci(github): add workflow"
)

PASS_COUNT=0
for msg in "${VALID_MESSAGES[@]}"; do
    if echo "$msg" | npx commitlint &> /dev/null; then
        echo -e "${GREEN}✓${NC} PASS: $msg"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗${NC} FAIL (unexpected): $msg"
    fi
done

echo ""
echo -e "${BOLD}Testing INVALID commit messages:${NC}"
echo ""

INVALID_MESSAGES=(
    "invalid message"
    "FEAT: uppercase type"
    "feat(Core): uppercase scope"
    "feat(core): Subject starts uppercase."
    "feat: missing scope"
    "add new feature"
    "feat(invalid-scope): wrong scope"
    "fix(core) missing colon"
)

FAIL_COUNT=0
for msg in "${INVALID_MESSAGES[@]}"; do
    if echo "$msg" | npx commitlint &> /dev/null; then
        echo -e "${RED}✗${NC} PASS (unexpected): $msg"
    else
        echo -e "${GREEN}✓${NC} FAIL (expected): $msg"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

echo ""
echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BOLD}Summary:${NC}"
echo "  Valid messages passed:   $PASS_COUNT/${#VALID_MESSAGES[@]}"
echo "  Invalid messages failed: $FAIL_COUNT/${#INVALID_MESSAGES[@]}"
echo ""

EXPECTED_PASS=${#VALID_MESSAGES[@]}
EXPECTED_FAIL=${#INVALID_MESSAGES[@]}

if [ $PASS_COUNT -eq $EXPECTED_PASS ] && [ $FAIL_COUNT -eq $EXPECTED_FAIL ]; then
    echo -e "${GREEN}${BOLD}✓ All tests passed!${NC}"
    echo ""
    echo "Commitlint is properly configured and working."
    exit 0
else
    echo -e "${RED}${BOLD}✗ Some tests failed${NC}"
    echo ""
    echo "Please check your commitlint configuration."
    exit 1
fi
