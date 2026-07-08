# 🤝 Como Submeter Patches à CloudFS

## 📋 Checklist Rápido

- [ ] Fork do repositório (`github.com/trouchet/cloudfs`)
- [ ] Clone seu fork localmente
- [ ] Crie branch com nome significativo
- [ ] Faça suas mudanças
- [ ] Rode `npm run lint` para formatar código
- [ ] Rode `./build_and_test.sh` para validar
- [ ] Commit com mensagem no formato correto
- [ ] Push para seu fork
- [ ] Abra Pull Request no repositório principal
- [ ] Aguarde code review

______________________________________________________________________

## 🚀 Passo-a-Passo Completo

### 1️⃣ Preparar Ambiente

```bash
# Fork do repositório (via GitHub UI)
# github.com/trouchet/cloudfs → botão "Fork"

# Clone seu fork
git clone https://github.com/SEU_USUARIO/cloudfs.git
cd cloudfs

# Adicionar remote upstream
git remote add upstream https://github.com/trouchet/cloudfs.git

# Instalar dependências de desenvolvimento
chmod +x setup_dev_tools.sh
./setup_dev_tools.sh
```

### 2️⃣ Criar Branch para seu Patch

```bash
# Atualizar branch main com últimas mudanças
git fetch upstream
git checkout main
git merge upstream/main

# Criar branch novo
# Formato: <tipo>/<descrição-curta>
git checkout -b fix/client-credentials-auth
# Ou para nova feature:
git checkout -b feat/sharepoint-delta-sync
```

### 3️⃣ Fazer Suas Mudanças

```bash
# Edite os arquivos necessários
vim src/providers/sharepoint/sharepoint_auth.cpp

# Adicione testes se necessário
vim test/sql/sharepoint_tests.sql
```

### 4️⃣ Validar Código

```bash
# 1. Verificar formatação e linting
npm run lint

# 2. Ou executar manualmente:
npm run format:cpp          # Formata C++
npm run format:cmake        # Formata CMake
npm run check:shell         # Valida shell scripts

# 3. Rodar testes completos
./build_and_test.sh

# 4. Testar manualmente (opcional)
duckdb -unsigned << EOF
LOAD './build/release/repository/v1.5.4/linux_amd64/cloudfs.duckdb_extension';
SELECT 1;  -- Sua query de teste
EOF
```

### 5️⃣ Commit com Mensagem Correta

**Formato obrigatório:**

```
<tipo>(<escopo>): <assunto>

[corpo opcional]

[rodapé opcional]
```

**Exemplos válidos:**

```bash
# Simples
git commit -m "fix(sharepoint): handle client credentials authentication"

# Com corpo
git commit -m "feat(auth): add client credentials flow for SharePoint

Implements Client Credentials OAuth2 flow for service-to-service auth.
Allows headless applications to authenticate without user interaction.

Closes #45"

# Breaking change
git commit -m "feat(auth)!: change OAuth flow to PKCE

BREAKING CHANGE: OAuth tokens now use PKCE. Existing tokens 
need regeneration."
```

**Tipos válidos:**

- `feat` - Nova feature
- `fix` - Correção de bug
- `docs` - Documentação
- `style` - Formatação (sem mudança lógica)
- `refactor` - Refatoração
- `perf` - Otimização de performance
- `test` - Testes
- `build` - Build system
- `ci` - CI/CD
- `chore` - Manutenção

**Escopos válidos:**

- `sharepoint`, `onedrive`, `gdrive`, `dropbox`, `sftp`, `vfs`
- `auth`, `cache`, `core`, `table-functions`, `http`
- `extension`, `agent`, `build`, `deps`, `docs`

### 6️⃣ Push e Abrir PR

```bash
# Push seu branch
git push origin fix/client-credentials-auth

# Ir a: github.com/trouchet/cloudfs
# Clicar em "Compare & pull request"
```

### 7️⃣ Descrição do Pull Request

**Template sugerido:**

```markdown
## 📝 Descrição
Breve descrição do que foi mudado e por quê.

## 🎯 Tipo de Mudança
- [ ] Bug fix
- [ ] Nova feature
- [ ] Breaking change
- [ ] Documentação

## 🧪 Testes
- [ ] Testes unitários adicionados
- [ ] Testes manuais executados
- [ ] Compatibilidade com versões anteriores verificada

## ✅ Checklist
- [ ] Código segue o style guide (npm run lint)
- [ ] Build passa (./build_and_test.sh)
- [ ] Commit message no formato correto
- [ ] Documentação atualizada
- [ ] Issue relacionada referenciada (Closes #123)

## 🔗 Issues Relacionadas
Closes #123
```

______________________________________________________________________

## 🚨 Problemas Comuns

### ❌ "Pre-commit hooks failed"

```bash
# Deixar pre-commit corrigir o que conseguir
pre-commit run --all-files

# Ver o que mudou
git diff

# Commitar as correções
git add -u
git commit -m "style: apply auto-formatting"
```

### ❌ "commitlint validation failed"

```bash
# Verificar formato da mensagem
echo "seu commit message" | npx commitlint

# Exemplos CORRETOS:
git commit -m "fix(sharepoint): handle null pointers"
git commit -m "feat(auth): implement client credentials"

# Exemplos ERRADOS (vai falhar):
git commit -m "Fixed sharepoint"  # ❌ Não é conventional commit
git commit -m "fix: ..."          # ❌ Falta escopo
```

### ❌ "Build failed"

```bash
# Verificar logs
./build_and_test.sh 2>&1 | tail -50

# Limpar cache e tentar novamente
rm -rf build .cache
./build_and_test.sh

# Se ainda falhar, pode estar incompatível com sua versão DuckDB
# Use: ./compile_for_current_duckdb.sh
```

### ❌ "Branch desatualizada com upstream"

```bash
# Trazer mudanças do repositório principal
git fetch upstream
git rebase upstream/main

# Se houver conflitos
git status  # Ver conflitos
# Resolver arquivos em seu editor
git add .
git rebase --continue

# Forçar push (cuidado!)
git push -f origin seu-branch
```

______________________________________________________________________

## 📊 CI/CD Automático

Quando você abre um PR, o GitHub automaticamente:

✅ **Testa em múltiplas plataformas:**

- Linux (gcc/clang)
- macOS (clang)
- Windows (MSVC)

✅ **Testa em múltiplas versões do DuckDB:**

- v1.5.4 (estável)
- Outras versões conforme necessário

✅ **Executa verificações:**

- Code formatting (clang-format)
- Linting (shellcheck, pylint, etc)
- Testes unitários
- Compatibilidade

❌ Se algo falhar, você receberá feedback. Faça as correções e push novamente.

______________________________________________________________________

## 🎁 Exemplo Prático: Adicionar Client Credentials

```bash
# 1. Preparar
git clone https://github.com/SEU_USUARIO/cloudfs.git
cd cloudfs && ./setup_dev_tools.sh

# 2. Criar branch
git checkout -b feat/sharepoint-client-credentials

# 3. Editar arquivo
vim src/providers/sharepoint/sharepoint_auth.cpp
# Adicionar classe SharePointClientCredentialsAuth

# 4. Formatar
npm run lint

# 5. Testar
./build_and_test.sh

# 6. Commit
git add src/providers/sharepoint/sharepoint_auth.cpp
git commit -m "feat(sharepoint): add client credentials auth flow

Implements Client Credentials OAuth2 flow for service-to-service
authentication. Allows headless applications to use CloudFS
SharePoint without browser interaction.

- New class: SharePointClientCredentialsAuth
- POST to /oauth2/v2.0/token with grant_type=client_credentials
- Token automatically cached by DuckDB secrets

Closes #42"

# 7. Push e PR
git push origin feat/sharepoint-client-credentials
# Abrir PR no GitHub
```

______________________________________________________________________

## 📚 Recursos Úteis

- [Conventional Commits](https://www.conventionalcommits.org/)
- [Git Workflow Guide](docs/DEVELOPMENT.md)
- [Build Quickstart](docs/BUILD_QUICKSTART.md)
- [CloudFS Features](README.md)

______________________________________________________________________

## ❓ Dúvidas?

- Abra uma Issue no GitHub
- Verifique a documentação em `docs/`
- Veja PRs anteriores para exemplos
