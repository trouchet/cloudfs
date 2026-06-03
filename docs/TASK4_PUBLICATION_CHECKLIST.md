# Task 4 - Checklist de Publicação no DuckDB Community Extensions

## Status Atual: ✅ Pronto para Publicação

### Pré-requisitos ✅

- [x] Repositório **público** no GitHub: `trouchet/cloudfs`
- [x] Licença open-source: MIT (verificar se LICENSE file existe)
- [x] Build baseado no `extension-template`: ✅ (`extension_config.cmake` existe)
- [x] Código commitado e limpo (commit:
  `dc364b16ed35f89f2526e839afa517b906466ef9`)

### Arquivos Criados ✅

- [x] `description.yml` criado na raiz do projeto
  - Commit SHA fixado: `dc364b16ed35f89f2526e839afa517b906466ef9`
  - Repositório: `trouchet/cloudfs`
  - Maintainer: `trouchet`

______________________________________________________________________

## Próximos Passos

### 1. Verificar Build Local ⏳

Antes de submeter, garantir que o build funciona:

```bash
# No WSL/Linux
cd ~/github/cloudfs
GEN=ninja make release

# Verificar saída
ls -lh build/release/extension/cloudfs/cloudfs.duckdb_extension
```

**Testar carregamento:**

```sql
LOAD 'build/release/extension/cloudfs/cloudfs.duckdb_extension';
SELECT cloudfs_version();
SELECT * FROM providers();
```

### 2. Criar LICENSE File (se não existir) ⏳

```bash
# Verificar
ls LICENSE || echo "Precisa criar LICENSE"

# Se não existir, criar MIT License
cat > LICENSE << 'EOF'
MIT License

Copyright (c) 2026 [Seu Nome]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
EOF
```

### 3. Setup GitHub Actions CI ⏳

O repositório precisa ter CI rodando com o template do DuckDB. Verificar se
`.github/workflows/` existe.

Se não existir, criar workflow baseado no extension-template:

- https://github.com/duckdb/extension-template/tree/main/.github/workflows

### 4. Commitar description.yml ⏳

```bash
cd ~/github/cloudfs
git add description.yml
git commit -m "docs: add DuckDB Community Extensions descriptor

Add description.yml for publication on DuckDB Community Extensions:
- Extension metadata and maintainer info
- Repository ref pinned to current commit
- Documentation with examples for all 6 providers
- Toolchain requirements (cmake, curl, openssl, libssh2)"

git push origin main
```

### 5. Fork e PR no Community Extensions ⏳

#### a) Fork do repositório

```bash
# No GitHub web UI
https://github.com/duckdb/community-extensions
# Clicar em "Fork"
```

#### b) Clone e criar branch

```bash
git clone https://github.com/trouchet/community-extensions
cd community-extensions
git checkout -b add-cloudfs-extension
```

#### c) Criar arquivo no local correto

```bash
mkdir -p extensions/cloudfs
cp ~/github/cloudfs/description.yml extensions/cloudfs/description.yml
```

#### d) Commit e push

```bash
git add extensions/cloudfs/description.yml
git commit -m "Add cloudfs extension

CloudFS provides 6 cloud storage protocols (SharePoint, OneDrive,
Google Drive, Dropbox, SFTP, VFS) as first-class DuckDB filesystems.

Features:
- spfs://, odfs://, gdfs://, dbxfs://, sftp://, vfs:// protocols
- Parquet, CSV, JSON, Delta Lake, Iceberg support
- OAuth2 authentication with DuckDB secrets
- cloudfs-agent for any Linux/macOS server
- Identical API to s3:// (glob, COPY TO, etc.)

Repository: https://github.com/trouchet/cloudfs"

git push origin add-cloudfs-extension
```

#### e) Abrir PR

```
Repositório: https://github.com/duckdb/community-extensions
Base: main
Head: trouchet:add-cloudfs-extension

Título: Add cloudfs extension

Descrição:
CloudFS extension for DuckDB - multi-cloud storage access

This PR adds the cloudfs extension which provides native support for:
- SharePoint (spfs://)
- OneDrive (odfs://)
- Google Drive (gdfs://)
- Dropbox (dbxfs://)
- SFTP (sftp://)
- VFS agent (vfs://)

All protocols work identically to s3:// with full support for Parquet, CSV,
JSON, Delta Lake, Iceberg, glob patterns, and COPY TO.

Repository: https://github.com/trouchet/cloudfs
Ref: dc364b16ed35f89f2526e839afa517b906466ef9
License: MIT
```

### 6. Aguardar Build e Review ⏳

- CI vai buildar para ~10 plataformas (~20 minutos)
- Responder comentários dos maintainers
- Iterar se necessário

### 7. Após Merge 🎉

Usuários poderão instalar com:

```sql
INSTALL cloudfs FROM community;
LOAD cloudfs;
SELECT * FROM ls('spfs://empresa.sharepoint.com/sites/X/Docs/');
```

Documentação estará em:

- https://duckdb.org/community_extensions/extensions/cloudfs

______________________________________________________________________

## Manutenção Futura

### Atualizar para novos releases do DuckDB

Quando um novo DuckDB release se aproximar:

```yaml
# Em description.yml
repo:
  github: trouchet/cloudfs
  ref: abc123def       # compatível com DuckDB v1.5.x (stable)
  ref_next: def456ghi  # compatível com DuckDB main (próximo release)
```

### Responder Issues

Usuários reportarão issues em:

- https://github.com/trouchet/cloudfs/issues
- https://github.com/duckdb/community-extensions/issues (mencionando @cloudfs)

______________________________________________________________________

## Suporte

- **Discord DuckDB**: https://discord.com/invite/tcvwpjfnZx
  - Canal: `#extension-development`
- **Community Extensions Docs**: https://duckdb.org/community_extensions/
- **Extension Template**: https://github.com/duckdb/extension-template

______________________________________________________________________

## Checklist Final

| Etapa                     | Status | Ação                               |
| ------------------------- | ------ | ---------------------------------- |
| Código limpo              | ✅     | Commit dc364b1                     |
| Repositório público       | ✅     | github.com/trouchet/cloudfs        |
| description.yml           | ✅     | Criado na raiz                     |
| LICENSE                   | ⏳     | Verificar se existe                |
| Build local               | ⏳     | Testar `make release`              |
| CI/Actions                | ⏳     | Verificar workflows                |
| Commitar description.yml  | ⏳     | `git commit && push`               |
| Fork community-extensions | ⏳     | No GitHub                          |
| Criar PR                  | ⏳     | extensions/cloudfs/description.yml |
| Aguardar review           | ⏳     | ~2-7 dias                          |
| Build CI passa            | ⏳     | ~20 min após PR                    |
| Merge                     | ⏳     | Maintainers aprovam                |
| Publicado                 | 🎉     | `INSTALL cloudfs FROM community`   |
