# 📋 RESUMO: Como Submeter Patches à CloudFS (Acabada de Publicar)

## 🎯 Resumo em 30 segundos

```bash
# 1. Fork do repo (clique em github.com/trouchet/cloudfs → Fork)

# 2. Clonar seu fork
git clone https://github.com/SEU_USUARIO/cloudfs.git && cd cloudfs

# 3. Editar código e validar
npm run lint && ./build_and_test.sh

# 4. Commit em Conventional Format
git commit -m "feat(sharepoint): descrição da mudança"

# 5. Push e abrir PR
git push origin seu-branch
# → Abrir PR em: github.com/trouchet/cloudfs
```

______________________________________________________________________

## 📚 Documentação Criada

Criei 3 documentos para ajudar:

| Arquivo                                                                  | Para quem     | Conteúdo                                                           |
| ------------------------------------------------------------------------ | ------------- | ------------------------------------------------------------------ |
| **[CONTRIBUTING_PT.md](./CONTRIBUTING_PT.md)**                           | 🇧🇷 Português  | Guia completo passo-a-passo (checklist, exemplos, troubleshooting) |
| **[SUBMIT_PATCH_QUICK_REFERENCE.md](./SUBMIT_PATCH_QUICK_REFERENCE.md)** | ⚡ Rápida     | Referência rápida (comandos essenciais, formatos)                  |
| **[submit_patch.sh](./submit_patch.sh)**                                 | 🤖 Automático | Script que automatiza os passos                                    |

______________________________________________________________________

## 🚀 3 Formas de Submeter

### Opção 1: Script Automático (Mais Fácil)

```bash
./submit_patch.sh
# O script guia você por todos os passos
```

### Opção 2: Manual Passo-a-Passo

```bash
# Ver guia completo
cat CONTRIBUTING_PT.md

# Ou seguir Quick Reference
cat SUBMIT_PATCH_QUICK_REFERENCE.md
```

### Opção 3: Só Copiar e Colar

```bash
# Setup
git clone https://github.com/SEU_USUARIO/cloudfs.git
cd cloudfs && ./setup_dev_tools.sh

# Branch
git checkout -b feat/minha-feature

# Editar...
vim src/providers/sharepoint/sharepoint_auth.cpp

# Validar
npm run lint && ./build_and_test.sh

# Commit (FORMATO OBRIGATÓRIO!)
git commit -m "feat(sharepoint): add minha feature

Descrição mais longa aqui

Closes #123"

# Push e PR
git push origin feat/minha-feature
```

______________________________________________________________________

## ✅ Requisitos Obrigatórios

Antes de submeter, TODOS os pontos abaixo precisam passar:

| Requisito               | Como Validar                | Status  |
| ----------------------- | --------------------------- | ------- |
| 1. **Código formatado** | `npm run lint`              | ❌ → ✅ |
| 2. **Build compila**    | `./build_and_test.sh`       | ❌ → ✅ |
| 3. **Commit format**    | `git commit -m "feat(...)"` | ❌ → ✅ |
| 4. **Sem conflitos**    | `git status` limpo          | ❌ → ✅ |

Se qualquer um falhar, seu PR será rejeitado.

______________________________________________________________________

## 🏷️ Formato de Commit (CRÍTICO!)

**DEVE seguir Conventional Commits:**

```
type(scope): subject

body

footer
```

### ✅ Exemplos Válidos

```bash
feat(sharepoint): add client credentials auth
fix(cache): prevent null pointer
docs(readme): update instructions
test(auth): add OAuth tests
perf(http): reduce overhead
```

### ❌ Exemplos Inválidos (SER REJETADO)

```bash
Fixed sharepoint auth          ❌ Não é conventional
feat: something                ❌ Sem escopo
fixed stuff                    ❌ Vago
feat(auth) - add feature       ❌ Usar `:` não `-`
```

______________________________________________________________________

## 🔄 Processo Automático do GitHub

Quando você abre um PR, GitHub automaticamente:

1. ✅ Roda testes em **3 plataformas** (Linux, macOS, Windows)
1. ✅ Valida **formatação** (clang-format, shellcheck, etc)
1. ✅ Executa **testes unitários**
1. ❌ Se algo falhar, você recebe feedback
1. 👥 Maintainers fazem **code review**

**Você pode fazer mais commits** enquanto aguarda review!

______________________________________________________________________

## 🆘 Problemas Comuns

### "commitlint failed"

```bash
# Mensagem de commit incorreta
# Solução: Reparar mensagem
git commit --amend -m "feat(sharepoint): seu commit correto"
```

### "npm run lint failed"

```bash
# Código não está formatado
# Solução: Formatar automaticamente
npm run format:cpp
npm run format:cmake
git add . && git commit --amend
```

### "Build failed"

```bash
# Build não compila
# Solução: Limpar cache e recompilar
rm -rf build .cache
./build_and_test.sh
```

______________________________________________________________________

## 🎓 Exemplo: Submeter Patch de Client Credentials

```bash
# Já temos um patch pronto em: patch_client_credentials.diff

# 1. Setup
git clone https://github.com/SEU_USUARIO/cloudfs.git
cd cloudfs && ./setup_dev_tools.sh

# 2. Branch
git checkout -b feat/sharepoint-client-credentials

# 3. Aplicar patch (ou editar manualmente)
patch -p1 < patch_client_credentials.diff

# 4. Validar
npm run lint
./build_and_test.sh

# 5. Commit
git add src/
git commit -m "feat(sharepoint): add client credentials authentication

Implements OAuth2 Client Credentials flow for service-to-service auth.

- New class: SharePointClientCredentialsAuth
- Token automatically cached
- Backward compatible with Device Code Flow

Closes #42"

# 6. Push
git push origin feat/sharepoint-client-credentials

# 7. Abrir PR em:
# github.com/trouchet/cloudfs/pull/new/feat/sharepoint-client-credentials
```

______________________________________________________________________

## 📊 Checklist Final

- [ ] Fiz fork do repositório
- [ ] Criei branch com nome descritivo
- [ ] Fiz minhas mudanças
- [ ] Rodei `npm run lint` ✅ passou
- [ ] Rodei `./build_and_test.sh` ✅ passou
- [ ] Mensagem de commit no formato `type(scope): message`
- [ ] Fiz push para meu fork
- [ ] Abri PR com descrição clara
- [ ] Aguardando review 🎉

______________________________________________________________________

## 🎯 Próximos Passos

1. **Agora**: Leia um dos 3 documentos criados
1. **Depois**: Faça seu primeiro patch
1. **Resultado**: Seu código no repositório oficial!

______________________________________________________________________

## 📖 Referências Rápidas

- **Conventional Commits**: https://www.conventionalcommits.org/
- **Git Workflow**: Ver [docs/DEVELOPMENT.md](./docs/DEVELOPMENT.md)
- **Build Guide**: Ver [docs/BUILD_QUICKSTART.md](./docs/BUILD_QUICKSTART.md)

______________________________________________________________________

## 💬 Perguntas?

- Verifique [CONTRIBUTING_PT.md](./CONTRIBUTING_PT.md)
- Ou abra uma Issue no GitHub
- Ou veja PRs anteriores para exemplos

**Bem-vindo como contribuidor!** 🚀
