#!/bin/bash
# Quick-install cloudfs-agent on a Debian/Ubuntu/RHEL VPS.
# Run as root: curl -sL https://your-org.github.io/cloudfs/setup.sh | bash

set -euo pipefail
ARCH=$(uname -m)
case $ARCH in x86_64) ARCH=amd64;; aarch64) ARCH=arm64;; *) echo "Unsupported arch: $ARCH"; exit 1;; esac
OS=$(uname -s | tr '[:upper:]' '[:lower:]')

RELEASE_URL="https://github.com/your-org/duckdb-cloudfs/releases/latest/download/cloudfs-agent-${OS}-${ARCH}"
echo "Downloading cloudfs-agent for ${OS}/${ARCH}..."
curl -fsSL "$RELEASE_URL" -o /usr/local/bin/cloudfs-agent
chmod +x /usr/local/bin/cloudfs-agent

# Create user and directories
useradd -r -s /bin/false cloudfs 2>/dev/null || true
mkdir -p /etc/cloudfs /data
chown cloudfs:cloudfs /etc/cloudfs /data
chmod 750 /etc/cloudfs

# Generate token if not exists
if [ ! -f /etc/cloudfs/env ]; then
    TOKEN=$(openssl rand -hex 32)
    echo "CLOUDFS_TOKEN=$TOKEN" > /etc/cloudfs/env
    echo "CLOUDFS_ROOT=/data"  >> /etc/cloudfs/env
    chmod 600 /etc/cloudfs/env
    chown cloudfs:cloudfs /etc/cloudfs/env
    echo ""
    echo "═══════════════════════════════════════════════"
    echo " Token saved to /etc/cloudfs/env"
    echo " CLOUDFS_TOKEN=$TOKEN"
    echo " Store this in your DuckDB secret:"
    echo ""
    echo "   CREATE PERSISTENT SECRET my_vps ("
    echo "       TYPE vfs,"
    echo "       TOKEN '$TOKEN'"
    echo "   );"
    echo "═══════════════════════════════════════════════"
fi

# Generate self-signed cert if not exists
if [ ! -f /etc/cloudfs/cert.pem ]; then
    openssl req -x509 -newkey rsa:4096 -keyout /etc/cloudfs/key.pem \
        -out /etc/cloudfs/cert.pem -sha256 -days 3650 -nodes \
        -subj "/CN=cloudfs-agent" 2>/dev/null
    chown cloudfs:cloudfs /etc/cloudfs/*.pem
    chmod 600 /etc/cloudfs/key.pem
    echo "Self-signed TLS cert generated."
fi

# Install and start systemd service
cp "$(dirname "$0")/cloudfs-agent.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now cloudfs-agent
systemctl status cloudfs-agent --no-pager

echo ""
echo "Agent running at port 8765. Connect with:"
echo "  vfs+tls://$(hostname -I | awk '{print $1}'):8765/path/to/data"
