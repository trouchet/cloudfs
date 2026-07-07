#!/bin/bash
# Compila CloudFS para a versão do DuckDB instalada

set -e

DUCKDB_VERSION=$(duckdb -version 2>/dev/null | awk '{print $2}')
echo "📦 Versão do DuckDB detectada: $DUCKDB_VERSION"

# Extrai versão principal (ex: v1.5.1 -> 1.5)
MAJOR_MINOR=$(echo "$DUCKDB_VERSION" | grep -oE '^[0-9]+\.[0-9]+' | head -1)

if [ -z "$MAJOR_MINOR" ]; then
  echo "❌ Não consegui detectar a versão do DuckDB"
  echo "   Use: make release"
  exit 1
fi

# Detecta versão exata do DuckDB submodule
cd "$(dirname "$0")"
DUCKDB_TAG=$(git -C duckdb describe --tags --abbrev=0 2>/dev/null || echo "unknown")
echo "🔍 Tag do DuckDB source: $DUCKDB_TAG"

echo "⚙️  Mudando para versão v${MAJOR_MINOR}..."
git -C duckdb checkout "v${MAJOR_MINOR}" 2>/dev/null || {
  echo "⚠️  Branch v${MAJOR_MINOR} não encontrado"
  echo "📋 Branches disponíveis:"
  git -C duckdb branch -a | grep "v${MAJOR_MINOR}" | head -5
  exit 1
}

echo "🗑️  Limpando build anterior..."
rm -rf build .cache 2>/dev/null || true

echo "🔨 Compilando CloudFS para v${MAJOR_MINOR}..."
make release

# Locala o arquivo compilado
EXTENSION=$(find build/release -name "cloudfs.duckdb_extension" -type f | head -1)

if [ -z "$EXTENSION" ]; then
  echo "❌ Compilação falhou - arquivo não encontrado"
  exit 1
fi

echo "✅ Extensão compilada: $EXTENSION"

# Copia para o local correto
EXT_DIR="$HOME/.duckdb/extensions/v${MAJOR_MINOR}/linux_amd64"
mkdir -p "$EXT_DIR"
cp "$EXTENSION" "$EXT_DIR/"

echo "📍 Instalada em: $EXT_DIR/cloudfs.duckdb_extension"
echo ""
echo "✨ Pronto! Use:"
echo "   duckdb"
echo "   D LOAD cloudfs;"
