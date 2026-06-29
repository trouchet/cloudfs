#!/bin/bash

echo "=== Quick Dependency Check ==="
echo ""

# Check if running in WSL/Linux
if [ ! -f /etc/os-release ]; then
    echo "❌ Not running in Linux/WSL"
    exit 1
fi

echo "✓ Running in Linux"
echo ""

# Check dependencies
echo "Checking build dependencies..."
MISSING=""

if ! command -v cmake &> /dev/null; then
    MISSING="$MISSING cmake"
fi

if ! command -v ninja &> /dev/null; then
    MISSING="$MISSING ninja-build"
fi

if ! dpkg -l 2>/dev/null | grep -q libssl-dev; then
    MISSING="$MISSING libssl-dev"
fi

if ! dpkg -l 2>/dev/null | grep -q libcurl4-openssl-dev; then
    MISSING="$MISSING libcurl4-openssl-dev"
fi

if ! dpkg -l 2>/dev/null | grep -q libssh2-1-dev; then
    MISSING="$MISSING libssh2-1-dev"
fi

if [ -n "$MISSING" ]; then
    echo "❌ Missing dependencies:$MISSING"
    echo ""
    echo "To install, run:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y$MISSING"
    exit 1
fi

echo "✓ All build dependencies installed"
echo ""

# Check project structure
cd /home/pingu/github/cloudfs || exit 1

echo "Checking project structure..."
if [ ! -f CMakeLists.txt ]; then
    echo "❌ CMakeLists.txt not found"
    exit 1
fi
echo "✓ CMakeLists.txt found"

if [ ! -d src ]; then
    echo "❌ src/ directory not found"
    exit 1
fi
echo "✓ src/ directory found"

# Check if duckdb submodule exists
if [ -d duckdb ]; then
    echo "✓ duckdb/ submodule found"
else
    echo "⚠️  duckdb/ submodule NOT found"
    echo "   This might be OK if CMakeLists.txt uses system DuckDB"
fi

echo ""
echo "Checking DuckDB installation..."
if command -v duckdb &> /dev/null; then
    DUCKDB_VERSION=$(duckdb --version 2>&1 | head -1)
    echo "✓ DuckDB installed: $DUCKDB_VERSION"
else
    echo "❌ DuckDB not found in PATH"
    exit 1
fi

echo ""
echo "=== ✓ Environment ready for build ==="
echo ""
echo "To build and test, run:"
echo "  chmod +x build_and_test.sh"
echo "  ./build_and_test.sh"
