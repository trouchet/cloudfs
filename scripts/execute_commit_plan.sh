#!/bin/bash
set -e

echo "=== CloudFS Commit Plan Execution ==="
echo ""
echo "Este script irá criar 4 commits seguindo o plano definido em COMMIT_PLAN.md"
echo ""

# Verificar se estamos em um repositório git
if [ ! -d .git ]; then
    echo "❌ Erro: Não estamos em um repositório Git"
    echo "Execute: git init"
    exit 1
fi

# Verificar se há mudanças não commitadas
if git diff --quiet && git diff --cached --quiet; then
    :
else
    echo "⚠️  Aviso: Há mudanças em staging ou working directory"
    echo ""
    git status --short
    echo ""
    read -p "Deseja continuar? Isso irá fazer git add dos arquivos planejados. (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Cancelado."
        exit 0
    fi
fi

# Verificar se é o primeiro commit
FIRST_COMMIT=false
if [ -z "$(git log --oneline 2>/dev/null)" ]; then
    echo "✓ Repositório vazio - incluindo commit inicial"
    FIRST_COMMIT=true
else
    echo "✓ Repositório já tem commits - aplicando apenas fixes e docs"
fi

echo ""
echo "─────────────────────────────────────────────────────────────"
echo ""

# Commit 0 (opcional): .gitignore
if [ -f .gitignore ] && ! git ls-files --error-unmatch .gitignore >/dev/null 2>&1; then
    echo "📝 Commit 0: Adicionando .gitignore"
    git add .gitignore
    git commit -m "chore: add .gitignore for build artifacts and IDE files"
    echo "✓ .gitignore commitado"
    echo ""
fi

# Commit 1 (opcional): Estrutura inicial do projeto
if [ "$FIRST_COMMIT" = true ]; then
    echo "📝 Commit 1: Estrutura inicial do projeto"
    
    # Adicionar estrutura base
    git add CMakeLists.txt extension_config.cmake vcpkg.json 2>/dev/null || true
    git add src/ docs/ agent/ test/ 2>/dev/null || true
    git add AGENT_HANDOVER.md 2>/dev/null || true
    
    git commit -m "chore: initial CloudFS DuckDB extension structure

Add base structure for CloudFS extension:
- CMake build configuration
- vcpkg dependency management
- Core filesystem abstraction (CloudFileSystem, ICloudBackend)
- Provider implementations (OneDrive, SharePoint, Google Drive, Dropbox, SFTP, VFS)
- Table functions (ls, stat, du)
- Agent service for VFS protocol
- Test infrastructure

This is a DuckDB extension that registers cloud storage protocols
(spfs://, odfs://, gdfs://, dbxfs://, sftp://, vfs://) as first-class
filesystems with support for Parquet, CSV, Delta Lake, and Iceberg."
    
    echo "✓ Estrutura inicial commitada"
    echo ""
fi

# Commit 2: Fix das table functions
echo "📝 Commit 2: Fix das table functions"
git add src/include/core/cloud_table_functions.hpp \
        src/core/cloud_table_functions.cpp

git commit -m "fix(table-functions): wire CloudFileSystem pointer to table functions

The ls(), stat(), and du() table functions were returning 0 rows because
they received nullptr instead of a valid CloudFileSystem pointer.

Changes:
- Add SetCloudFS() declaration to cloud_table_functions.hpp (line 60)
- Fix RegisterCloudTableFunctions() signature - remove CloudFileSystem* param (line 66)
- Use g_tf_cfs instead of nullptr when registering functions (lines 400-402)

The module-level g_tf_cfs pointer is now properly set via SetCloudFS() during
extension load (called from cloudfs_extension.cpp:226), and all three table
functions now receive the correct CloudFileSystem instance.

Fixes: Table functions now work correctly with DuckDB v1.4.0+
Related: AGENT_HANDOVER.md Task 1"

echo "✓ Table functions fix commitado"
echo ""

# Commit 3: Documentação
echo "📝 Commit 3: Documentação"
git add BUILD_QUICKSTART.md TASK1_STATUS.md

git commit -m "docs: add comprehensive build and test documentation

Add two documentation files to help developers build and test the extension:

BUILD_QUICKSTART.md:
- Step-by-step build instructions
- Dependency installation guide
- Quick test procedures
- Troubleshooting common issues
- Expected output examples

TASK1_STATUS.md:
- Detailed analysis of the 3 bugs fixed in table functions
- Before/after code comparison
- Complete test script with expected results
- Compilation instructions

These guides address the DuckDB v0.0.1 -> v1.4.0 migration and document
the table functions fix from Task 1."

echo "✓ Documentação commitada"
echo ""

# Commit 4: Scripts de build/test
echo "📝 Commit 4: Scripts de build e test"
git add build_and_test.sh check_deps.sh

git commit -m "chore: add build and dependency check scripts

Add automation scripts for build and testing workflow:

build_and_test.sh:
- Automated full build pipeline (CMake + Ninja)
- Comprehensive test suite for all table functions (ls, stat, du)
- VFS agent lifecycle management
- Test data generation and cleanup
- Expected output validation

check_deps.sh:
- Pre-build dependency verification
- Checks for cmake, ninja, libssl-dev, libcurl, libssh2
- DuckDB installation validation
- Project structure verification
- User-friendly error messages with install commands

Usage:
  chmod +x check_deps.sh build_and_test.sh
  ./check_deps.sh          # verify environment
  ./build_and_test.sh      # build + test"

echo "✓ Scripts commitados"
echo ""

# Commit 5: Plano de commits (este documento)
echo "📝 Commit 5: Plano de commits"
git add COMMIT_PLAN.md

git commit -m "docs: add commit strategy documentation

Add COMMIT_PLAN.md documenting the commit breakdown strategy:
- Atomic commit structure
- Conventional commit messages
- Execution order
- Verification checklist

This document explains the reasoning behind splitting the Task 1 changes
into focused, reviewable commits."

echo "✓ Plano de commits commitado"
echo ""

echo "─────────────────────────────────────────────────────────────"
echo ""
echo "✅ Todos os commits foram criados com sucesso!"
echo ""
echo "📊 Histórico:"
git log --oneline --graph -6

echo ""
echo "🔍 Para revisar os commits:"
echo "  git log --stat"
echo "  git show HEAD~4  # Ver primeiro commit (fix)"
echo "  git show HEAD~3  # Ver segundo commit (docs)"
echo "  git show HEAD~2  # Ver terceiro commit (scripts)"
echo ""
echo "🚀 Para fazer push:"
echo "  git remote add origin <URL>"
echo "  git push -u origin master"
echo ""
