#!/bin/bash
set -e

echo "=== CloudFS Build & Test Script ==="
echo ""

cd /home/pingu/github/cloudfs

# Check if dependencies are installed
echo "Checking dependencies..."
if ! dpkg -l | grep -q libssl-dev; then
    echo "⚠️  libssl-dev not installed. Run: sudo apt-get install -y libssl-dev libcurl4-openssl-dev libssh2-1-dev"
    exit 1
fi

echo "✓ Dependencies OK"
echo ""

# Clean old build
echo "Cleaning old build..."
rm -rf build
mkdir -p build

# Configure with CMake
echo "Configuring CMake..."
cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "Building extension..."
ninja

echo ""
echo "✓ Build complete!"
echo ""

# Check the built extension
if [ -f cloudfs.duckdb_extension ]; then
    echo "✓ Extension binary: build/cloudfs.duckdb_extension"
    ls -lh cloudfs.duckdb_extension
else
    echo "❌ Extension binary not found!"
    exit 1
fi

# Copy to parent directory
echo ""
echo "Copying extension to project root..."
cp cloudfs.duckdb_extension ../
cd ..

echo ""
echo "=== Testing Extension ==="
echo ""

# Generate token
TOKEN=$(openssl rand -hex 16)
echo "Token: $TOKEN"

# Create test data
mkdir -p /tmp/vfs_test/data
echo "test content from build_and_test.sh" > /tmp/vfs_test/data/test.txt
echo "another file" > /tmp/vfs_test/data/file2.txt

# Start agent
echo ""
echo "Starting cloudfs-agent..."
chmod +x ./cloudfs-agent
./cloudfs-agent --token "$TOKEN" --port 19876 --root /tmp/vfs_test &
AGENT_PID=$!
sleep 2

# Test extension
echo ""
echo "=== Test 1: Load extension and check version ==="
duckdb -unsigned -c "LOAD './cloudfs.duckdb_extension'; SELECT cloudfs_version();"

echo ""
echo "=== Test 2: Check providers ==="
duckdb -unsigned -c "LOAD './cloudfs.duckdb_extension'; SELECT providers();"

echo ""
echo "=== Test 3: Create VFS secret ==="
duckdb -unsigned -c "
LOAD './cloudfs.duckdb_extension';
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');
SELECT 'Secret created successfully' as result;
"

echo ""
echo "=== Test 4: ls() function - List files ==="
duckdb -unsigned -c "
LOAD './cloudfs.duckdb_extension';
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');
SELECT name, type, size_pretty FROM ls('vfs://localhost:19876/data/');
"

echo ""
echo "=== Test 5: stat() function - File metadata ==="
duckdb -unsigned -c "
LOAD './cloudfs.duckdb_extension';
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');
SELECT name, size, type FROM stat('vfs://localhost:19876/data/test.txt');
"

echo ""
echo "=== Test 6: du() function - Disk usage ==="
duckdb -unsigned -c "
LOAD './cloudfs.duckdb_extension';
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');
SELECT directory, file_count, size_pretty FROM du('vfs://localhost:19876/');
"

echo ""
echo "=== Test 7: Read file content via vfs:// ==="
duckdb -unsigned -c "
LOAD './cloudfs.duckdb_extension';
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');
SELECT * FROM read_text('vfs://localhost:19876/data/test.txt');
"

# Cleanup
echo ""
echo "Cleaning up..."
kill $AGENT_PID 2>/dev/null || true
rm -rf /tmp/vfs_test

echo ""
echo "=== ✓ All tests complete! ==="
