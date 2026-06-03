# ✅ Configuração de Commitlint e Pre-commit - Resumo

## 📦 Arquivos Criados

### Configuração de Commitlint
- **`.commitlintrc.json`** - Configuração JSON (alternativa simples)
- **`commitlint.config.js`** - Configuração JavaScript (mais flexível, com comentários)
- **`package.json`** - Dependências Node.js e scripts npm

### Configuração de Pre-commit
- **`.pre-commit-config.yaml`** - Configuração dos hooks de pre-commit
- **`.clang-format`** - Estilo de código C++ (baseado em LLVM)
- **`.editorconfig`** - Configuração universal de editores
- **`.secrets.baseline`** - Será criado no setup (baseline para detecção de secrets)

### Scripts de Automação
- **`setup_dev_tools.sh`** - Instalação completa de ferramentas de desenvolvimento
- **`validate_dev_setup.sh`** - Validação do ambiente de desenvolvimento
- **`Makefile`** - Comandos make para tarefas comuns

### Documentação
- **`DEVELOPMENT.md`** - Guia completo de desenvolvimento
- **`.gitignore`** - Atualizado com padrões de Node.js e Python

---

## 🚀 Quick Start

### 1. Instalar Ferramentas

```bash
# Tornar script executável e rodar
chmod +x setup_dev_tools.sh
./setup_dev_tools.sh
```

**OU** usando Make:
```bash
make dev-setup
```

### 2. Validar Instalação

```bash
# Verificar se tudo foi instalado corretamente
chmod +x validate_dev_setup.sh
./validate_dev_setup.sh
```

**OU**:
```bash
make validate
```

### 3. Usar no Dia-a-Dia

```bash
# Fazer mudanças no código
vim src/core/cloud_filesystem.cpp

# Formatar automaticamente
make format

# Commitar (hooks rodam automaticamente)
git add .
git commit -m "feat(core): add new filesystem feature"
```

---

## 🎯 Ferramentas Instaladas

| Ferramenta | Propósito | Comando |
|------------|-----------|---------|
| **pre-commit** | Framework de Git hooks | `pre-commit run --all-files` |
| **commitlint** | Validação de mensagens de commit | `npx commitlint` |
| **clang-format** | Formatação de código C++ | `clang-format -i file.cpp` |
| **shellcheck** | Linting de shell scripts | `shellcheck script.sh` |
| **cmake-format** | Formatação de CMake | `cmake-format -i CMakeLists.txt` |
| **detect-secrets** | Detecção de secrets no código | `detect-secrets scan` |
| **mdformat** | Formatação de Markdown | `mdformat file.md` |

---

## 📝 Formato de Commit

### Estrutura

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

### Tipos Válidos

- **feat** - Nova funcionalidade
- **fix** - Correção de bug
- **docs** - Apenas documentação
- **style** - Formatação de código
- **refactor** - Refatoração
- **perf** - Melhoria de performance
- **test** - Testes
- **build** - Sistema de build
- **ci** - CI/CD
- **chore** - Manutenção
- **revert** - Reverter commit

### Escopos Válidos

`core`, `table-functions`, `auth`, `cache`, `http`, `providers`, `onedrive`, `sharepoint`, `gdrive`, `dropbox`, `sftp`, `vfs`, `agent`, `extension`, `build`, `deps`, `docs`, `tests`, `scripts`

### Exemplos

✅ **Válidos**:
```bash
git commit -m "fix(table-functions): handle null CloudFileSystem pointer"
git commit -m "feat(onedrive): add delta sync support"
git commit -m "docs(readme): update installation instructions"
git commit -m "chore(deps): update DuckDB to v1.4.0"
```

❌ **Inválidos**:
```bash
git commit -m "Fixed bug"                    # Sem tipo/escopo
git commit -m "FEAT: new feature"            # Tipo em maiúscula
git commit -m "feat(Core): add feature"      # Escopo em maiúscula
git commit -m "feat(core): Add feature."     # Subject começa com maiúscula e tem ponto
```

---

## 🪝 Hooks Configurados

### Pre-commit (antes de commitar)

1. **Verificação de arquivos**
   - Tamanho máximo (1MB)
   - Newline no final
   - Trailing whitespace
   - Conflitos de merge

2. **Formatação de código**
   - C++ (clang-format)
   - CMake (cmake-format)
   - Shell (shellcheck)
   - Markdown (mdformat)
   - Go (gofmt, govet)

3. **Segurança**
   - Detecção de secrets
   - Verificação de sintaxe JSON/YAML

4. **Prevenção**
   - Não permite commit direto em main/master
   - Verifica conflitos de case-insensitive filesystems

### Commit-msg (na mensagem de commit)

- Valida formato conventional commits
- Verifica tipo e escopo
- Valida comprimento (max 100 caracteres no subject)
- Verifica que subject não termina com ponto

---

## 🔧 Comandos Make Úteis

```bash
make help              # Mostrar todos os comandos disponíveis
make dev-setup         # Setup completo do ambiente de dev
make validate          # Validar configuração
make build             # Compilar extensão
make test              # Rodar testes
make format            # Formatar todo o código
make lint              # Rodar todos os linters
make check-all         # Rodar todas as verificações
make clean             # Limpar build artifacts
make commit-lint       # Validar último commit
```

---

## 🎨 Estilo de Código C++

### Configuração (.clang-format)

- **Base**: LLVM style
- **Indentação**: 4 espaços
- **Largura da linha**: 100 caracteres
- **Ponteiros**: alinhados à esquerda (`int* ptr`)
- **Chaves**: attached style (na mesma linha)
- **Includes**: ordenados e agrupados

### Formatar Código

```bash
# Arquivo único
clang-format -i src/core/cloud_filesystem.cpp

# Todos os arquivos C++
make format

# Apenas verificar (sem modificar)
make check-format
```

---

## 🔍 Validações Automáticas

### Ao fazer `git commit`:

1. ✅ Código C++ formatado corretamente
2. ✅ Shell scripts sem erros (shellcheck)
3. ✅ CMake formatado
4. ✅ Markdown formatado
5. ✅ Nenhum secret commitado
6. ✅ Arquivos terminam com newline
7. ✅ Sem trailing whitespace
8. ✅ Mensagem de commit no formato correto

### Bypass (use com cuidado!)

```bash
# Pular hooks de pre-commit (não recomendado)
git commit --no-verify -m "wip: work in progress"

# Pular hook específico
SKIP=clang-format git commit -m "fix: emergency hotfix"
```

---

## 🐛 Troubleshooting

### Problema: "commitlint not found"

**Solução**:
```bash
npm install
npx commitlint --version
```

### Problema: "clang-format not found"

**Solução**:
```bash
# Linux/WSL
sudo apt-get install clang-format

# macOS
brew install clang-format
```

### Problema: "pre-commit hook failed"

**Solução**:
```bash
# Ver o que falhou
pre-commit run --all-files --verbose

# Auto-corrigir formatação
make format
git add -u

# Tentar commit novamente
git commit -m "fix(core): your message"
```

### Problema: "Secret detected"

**Solução**:
```bash
# Se for falso positivo, atualizar baseline
detect-secrets scan > .secrets.baseline

# Ou adicionar pragma no arquivo:
# pragma: allowlist secret
```

---

## 📚 Documentação Adicional

- **[DEVELOPMENT.md](DEVELOPMENT.md)** - Guia completo de desenvolvimento
- **[COMMIT_PLAN.md](COMMIT_PLAN.md)** - Estratégia de commits
- **[BUILD_QUICKSTART.md](BUILD_QUICKSTART.md)** - Guia de build

---

## ✅ Checklist de Verificação

Antes de fazer PR:

- [ ] `make validate` passa sem erros
- [ ] `make lint` sem warnings
- [ ] `make check-format` sem erros
- [ ] `make test` todos os testes passam
- [ ] Commits seguem conventional commits
- [ ] Sem secrets commitados
- [ ] Documentação atualizada (se necessário)

---

## 🎓 Recursos Externos

- [Conventional Commits](https://www.conventionalcommits.org/)
- [commitlint Documentation](https://commitlint.js.org/)
- [pre-commit Framework](https://pre-commit.com/)
- [clang-format Options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [Semantic Versioning](https://semver.org/)

---

## 🚀 Próximos Passos

1. **Rodar setup**:
   ```bash
   make dev-setup
   ```

2. **Validar**:
   ```bash
   make validate
   ```

3. **Fazer primeiro commit seguindo o padrão**:
   ```bash
   git add .
   git commit -m "chore(build): configure commitlint and pre-commit hooks"
   ```

4. **Verificar que funciona**:
   ```bash
   # Tentar commit inválido (deve falhar)
   git commit --allow-empty -m "invalid commit"
   
   # Tentar commit válido (deve passar)
   git commit --allow-empty -m "test(build): validate hooks"
   ```

---

**Configuração completa! Ambiente de desenvolvimento profissional pronto para uso! 🎉**
