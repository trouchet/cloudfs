# 📋 Plano de Commits - CloudFS Table Functions Fix

Este documento define a estratégia de commits atômicos para as correções da Task 1 (table functions) do projeto CloudFS.

---

## 🎯 Objetivo

Dividir as mudanças em commits pequenos, focados e semanticamente coerentes, facilitando:
- ✅ Code review
- ✅ Debugging (git bisect)
- ✅ Reversão seletiva
- ✅ Histórico compreensível

---

## 📦 Commits Propostos

### Commit 1: `fix(table-functions): wire CloudFileSystem pointer to table functions`

**Problema**: As table functions `ls()`, `stat()` e `du()` retornavam 0 linhas porque recebiam `nullptr` ao invés do ponteiro válido para `CloudFileSystem`.

**Solução**: 
1. Adicionar declaração `SetCloudFS()` no header
2. Corrigir assinatura de `RegisterCloudTableFunctions()` (remover parâmetro CloudFileSystem*)
3. Usar `g_tf_cfs` ao invés de `nullptr` na registro das funções

**Arquivos**:
```
src/include/core/cloud_table_functions.hpp
src/core/cloud_table_functions.cpp
```

**Comando**:
```bash
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
```

---

### Commit 2: `docs: add comprehensive build and test documentation`

**Objetivo**: Documentar o processo de build, testes e troubleshooting para desenvolvedores.

**Conteúdo**:
- Guia rápido de build (`BUILD_QUICKSTART.md`)
- Status detalhado da Task 1 com procedimentos de teste (`TASK1_STATUS.md`)

**Arquivos**:
```
BUILD_QUICKSTART.md
TASK1_STATUS.md
```

**Comando**:
```bash
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
```

---

### Commit 3: `chore: add build and dependency check scripts`

**Objetivo**: Automatizar o processo de build, testes e verificação de dependências.

**Conteúdo**:
- Script completo de build + testes (`build_and_test.sh`)
- Script de verificação de dependências (`check_deps.sh`)

**Arquivos**:
```
build_and_test.sh
check_deps.sh
```

**Comando**:
```bash
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
```

---

### Commit 4 (Opcional): `chore: add initial project structure`

**Objetivo**: Se este for o primeiro commit do repositório, adicionar a estrutura base do projeto.

**Arquivos**:
```
CMakeLists.txt
extension_config.cmake
vcpkg.json
src/
docs/
agent/
test/
```

**Comando**:
```bash
# Adicionar tudo exceto build artifacts
git add CMakeLists.txt extension_config.cmake vcpkg.json
git add src/ docs/ agent/ test/ AGENT_HANDOVER.md

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
```

---

## 🔄 Ordem de Execução

### Se o repositório já tem commits anteriores:
```bash
# 1. Aplicar fix do código
git add src/include/core/cloud_table_functions.hpp src/core/cloud_table_functions.cpp
git commit -F <(cat <<EOF
fix(table-functions): wire CloudFileSystem pointer to table functions

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
Related: AGENT_HANDOVER.md Task 1
EOF
)

# 2. Adicionar documentação
git add BUILD_QUICKSTART.md TASK1_STATUS.md
git commit -F <(cat <<EOF
docs: add comprehensive build and test documentation

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
the table functions fix from Task 1.
EOF
)

# 3. Adicionar scripts de build/test
git add build_and_test.sh check_deps.sh
git commit -F <(cat <<EOF
chore: add build and dependency check scripts

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
  ./build_and_test.sh      # build + test
EOF
)
```

### Se este é o primeiro commit (repositório vazio):
```bash
# 1. Commit inicial com estrutura base
git add CMakeLists.txt extension_config.cmake vcpkg.json \
        src/ docs/ agent/ test/ AGENT_HANDOVER.md
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

# 2-4. Seguir os commits 1-3 acima
```

---

## 🚫 O Que NÃO Commitar

**Build artifacts** (adicionar ao `.gitignore`):
```gitignore
# Build outputs
build/
*.duckdb_extension
cloudfs-agent

# Test artifacts
test_extension.sh

# IDE
.vscode/
.idea/

# OS
.DS_Store
*.Zone.Identifier
```

**Comando**:
```bash
# Criar .gitignore
cat > .gitignore << 'EOF'
# Build outputs
build/
*.duckdb_extension
cloudfs-agent

# Test artifacts  
test_extension.sh

# IDE
.vscode/
.idea/

# OS
.DS_Store
*.Zone.Identifier
EOF

git add .gitignore
git commit -m "chore: add .gitignore for build artifacts and IDE files"
```

---

## 📊 Verificação

Após executar os commits, verifique o histórico:

```bash
git log --oneline --graph
```

**Output esperado**:
```
* a1b2c3d chore: add build and dependency check scripts
* d4e5f6g docs: add comprehensive build and test documentation
* h7i8j9k fix(table-functions): wire CloudFileSystem pointer to table functions
* k0l1m2n chore: add .gitignore for build artifacts and IDE files
* n3o4p5q chore: initial CloudFS DuckDB extension structure
```

---

## 🎯 Benefícios Desta Estrutura

1. **Commits atômicos**: Cada commit tem um propósito único e claro
2. **Reversível**: Pode reverter documentação sem afetar código
3. **Bisect-friendly**: Fácil encontrar qual commit introduziu um bug
4. **Code review**: Reviewer pode analisar cada mudança separadamente
5. **Conventional commits**: Mensagens seguem padrão `type(scope): description`
6. **Contexto completo**: Commit messages explicam o "porquê", não só o "o quê"

---

## ✅ Checklist

Antes de fazer push:

- [ ] Verifiquei que cada commit compila individualmente
- [ ] Mensagens de commit seguem conventional commits
- [ ] `.gitignore` está configurado corretamente
- [ ] Build artifacts não estão sendo commitados
- [ ] Histórico está limpo e compreensível (`git log --oneline`)
- [ ] Cada commit tem um propósito único e claro

---

**Autor**: GitHub Copilot  
**Data**: 2026-06-03  
**Contexto**: Task 1 - Table Functions Fix (AGENT_HANDOVER.md)
