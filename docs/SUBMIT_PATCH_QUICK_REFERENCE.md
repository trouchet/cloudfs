# 🎯 Quick Reference: Submeter Patches à CloudFS

## ⚡ Versão Super Rápida (5 minutos)

```bash
# 1. Fork em: github.com/trouchet/cloudfs → "Fork"

# 2. Clone e setup
git clone https://github.com/SEU_USUARIO/cloudfs.git && cd cloudfs
./setup_dev_tools.sh

# 3. Criar branch
git checkout -b feat/sua-feature

# 4. Editar e testar
npm run lint && ./build_and_test.sh

# 5. Commit (formato obrigatório!)
git commit -m "feat(sharepoint): sua descrição curta

Descrição mais longa se necessário.

Closes #123"

# 6. Push e PR
git push origin feat/sua-feature
# Abrir PR em: github.com/trouchet/cloudfs
```

______________________________________________________________________

## 📋 Checklist de Submissão

**Antes de commitar:**

- [ ] `npm run lint` ✅ passa
- [ ] `./build_and_test.sh` ✅ passa
- [ ] Mensagem de commit no formato `type(scope): message`
- [ ] Nenhuma quebra de compatibilidade não-documentada

**Antes de fazer push:**

- [ ] Branch está atualizado com `upstream/main`
- [ ] Commits fazem sentido lógico (não muitos commits triviais)
- [ ] Sem arquivos desnecessários (build/, .cache/, etc)

**Antes de abrir PR:**

- [ ] Descrição clara do que foi mudado e por quê
- [ ] Links para issues relacionadas
- [ ] Screenshots/exemplos se aplicável

______________________________________________________________________

## 🏷️ Formato de Mensagem de Commit

### ✅ Correto

```
feat(sharepoint): add client credentials flow
fix(cache): prevent null pointer dereference
docs(readme): update build instructions
test(auth): add OAuth2 integration tests
perf(http): reduce connection overhead
```

### ❌ Errado

```
updated code                    # Vago
fix bug                         # Sem tipo/escopo
feat: awesome feature          # Sem escopo
Fixed SharePoint auth          # Não é lowercase
```

______________________________________________________________________

## 🔧 Tipos de Commit

| Tipo       | Quando usar             | Exemplo                                   |
| ---------- | ----------------------- | ----------------------------------------- |
| `feat`     | Nova funcionalidade     | `feat(gdrive): add folder sync`           |
| `fix`      | Correção de bug         | `fix(auth): handle expired tokens`        |
| `docs`     | Documentação            | `docs: add OAuth2 guide`                  |
| `style`    | Formatação (sem lógica) | `style: fix indentation`                  |
| `refactor` | Reorganizar sem mudança | `refactor(core): simplify error handling` |
| `perf`     | Otimização              | `perf(cache): use memory pool`            |
| `test`     | Testes                  | `test(providers): add SharePoint tests`   |
| `build`    | Sistema de build        | `build: update CMake config`              |
| `ci`       | CI/CD                   | `ci: add clang-tidy checks`               |
| `chore`    | Manutenção              | `chore(deps): update openssl`             |

______________________________________________________________________

## 🏢 Escopos Válidos

**Por Tipo de Mudança:**

- **Provedores**: `sharepoint`, `onedrive`, `gdrive`, `dropbox`, `sftp`
- **Sistema**: `auth`, `cache`, `core`, `http`, `table-functions`
- **Projeto**: `extension`, `agent`, `build`, `ci`, `deps`, `docs`

**Exemplo:**

```
feat(sharepoint): add client credentials
     ^^^^^^^^^^^ escopo = provedor SharePoint
```

______________________________________________________________________

## 🚀 Comandos Essenciais

```bash
# Setup inicial
git remote add upstream https://github.com/trouchet/cloudfs.git

# Atualizar branch
git fetch upstream
git rebase upstream/main

# Validar antes de commitar
npm run format:cpp              # Formata C++
npm run format:cmake            # Formata CMake
npm run check:shell             # Valida shell scripts
npm run lint                    # Tudo junto
./build_and_test.sh             # Build + testes

# Ver o que vai ser commitado
git diff --cached

# Alterar último commit
git commit --amend              # Se esqueceu algo

# Desfazer último commit (manter mudanças)
git reset --soft HEAD~1

# Limpar tudo e começar fresh
git reset --hard upstream/main
```

______________________________________________________________________

## 🔄 Workflow Típico

```
┌─ main (upstream)
│
├─ Seu fork clonado
│
├─ git checkout -b feat/sua-feature
│
├─ [editar arquivos]
│  ├─ npm run lint
│  ├─ ./build_and_test.sh
│  ├─ git add .
│  ├─ git commit -m "feat(...): ..."
│
├─ git push origin feat/sua-feature
│
├─ Abrir PR em GitHub
│
├─ [aguardar review]
│
├─ [receber feedback]
│  ├─ [fazer mudanças]
│  ├─ git add .
│  ├─ git commit -m "fix(review): ..."
│  ├─ git push origin feat/sua-feature
│
├─ PR aprovado ✅
│
└─ Merge!
```

______________________________________________________________________

## ❓ Problemas Comuns

### "commitlint validation failed"

```bash
# Verificar formato
echo "fix(sharepoint): seu mensagem" | npx commitlint

# Reparar mensagem
git commit --amend -m "fix(sharepoint): seu mensagem"
```

### "Build failed"

```bash
# Limpar cache
rm -rf build .cache

# Recompilar
./build_and_test.sh

# Se ainda falhar, usar script de compatibilidade
./compile_for_current_duckdb.sh
```

### "Branch desatualizada"

```bash
git fetch upstream
git rebase upstream/main
git push -f origin seu-branch
```

______________________________________________________________________

## 📚 Documentação Referência

| Arquivo                                              | Conteúdo                   |
| ---------------------------------------------------- | -------------------------- |
| [CONTRIBUTING_PT.md](CONTRIBUTING_PT.md)             | Guia completo em Português |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)           | Guia de desenvolvimento    |
| [docs/BUILD_QUICKSTART.md](docs/BUILD_QUICKSTART.md) | Build rápido               |
| [README.md](README.md)                               | Visão geral do projeto     |

______________________________________________________________________

## 🎓 Exemplo Passo-a-Passo: Client Credentials

```bash
# 1. Setup
git clone https://github.com/SEU_USUARIO/cloudfs.git
cd cloudfs && ./setup_dev_tools.sh

# 2. Branch
git checkout -b feat/sharepoint-client-credentials

# 3. Editar (seu patch)
vim src/providers/sharepoint/sharepoint_auth.cpp
# Adicionar classe SharePointClientCredentialsAuth

# 4. Validar
npm run lint
./build_and_test.sh

# 5. Testar manualmente
duckdb -unsigned << EOF
LOAD './build/release/repository/v1.5.4/linux_amd64/cloudfs.duckdb_extension';
-- Seu teste aqui
EOF

# 6. Commit
git add src/
git commit -m "feat(sharepoint): add client credentials auth flow

Implements OAuth2 Client Credentials flow for service-to-service auth.

- New class: SharePointClientCredentialsAuth
- Token automatically cached
- Backward compatible

Closes #42"

# 7. Push
git push origin feat/sharepoint-client-credentials

# 8. PR
# Ir a: github.com/trouchet/cloudfs/pull/new/feat/sharepoint-client-credentials
```

______________________________________________________________________

## 💡 Dicas Profissionais

✅ **Faça:**

- Commits pequenos e focused
- Mensagens descritivas
- Teste antes de fazer push
- Rebase, não merge (ao atualizar)
- Reference issues e PRs

❌ **Evite:**

- Commits gigantes com 10 mudanças
- "Fixed stuff" como mensagem
- Merge commits (use rebase)
- Push força sem necessidade
- Dados sensíveis ou binários

______________________________________________________________________

## 🎯 Pronto para Começar?

1. **Rápido:** Use `./submit_patch.sh` (script automático)
1. **Manual:** Siga [CONTRIBUTING_PT.md](CONTRIBUTING_PT.md)
1. **Dúvidas:** Veja [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)

**Bem-vindo como contribuidor!** 🎉
