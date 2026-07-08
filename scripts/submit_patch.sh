#!/bin/bash
# 🚀 Script de submissão do patch: Client Credentials para SharePoint
# Este script automatiza os passos para submeter seu patch à CloudFS

set -e

echo "════════════════════════════════════════════════════════════"
echo "  🚀 Submeter Patch: Client Credentials Auth para SharePoint"
echo "════════════════════════════════════════════════════════════"
echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 1: Verificar Prerequisites
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 1: Verificando prerequisites..."
echo ""

if ! command -v git &> /dev/null; then
    echo "❌ Git não instalado"
    exit 1
fi

if ! command -v npm &> /dev/null; then
    echo "⚠️  npm não instalado. Execute: ./setup_dev_tools.sh"
    exit 1
fi

echo "✅ git e npm encontrados"
echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 2: Verificar repositório
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 2: Verificando repositório..."
echo ""

REMOTE=$(git remote get-url origin)
echo "Remote origin: $REMOTE"

if [[ ! "$REMOTE" == *"cloudfs"* ]]; then
    echo "❌ Não está em repositório cloudfs"
    exit 1
fi

# Verificar se é um fork pessoal
if [[ ! "$REMOTE" == *"github.com"* ]]; then
    echo "❌ Remote não é GitHub"
    exit 1
fi

GITHUB_USER=$(echo "$REMOTE" | sed 's|.*github.com/||' | sed 's|/.*||')
echo "👤 Usuário GitHub: $GITHUB_USER"
echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 3: Atualizar main
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 3: Atualizando branch main..."
echo ""

if ! git remote | grep -q upstream; then
    echo "➕ Adicionando remote 'upstream'..."
    git remote add upstream https://github.com/trouchet/cloudfs.git
fi

git fetch upstream
echo "✅ Fetch upstream completo"
echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 4: Criar branch
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 4: Criando branch de feature..."
echo ""

BRANCH_NAME="feat/sharepoint-client-credentials"
echo "Nome do branch: $BRANCH_NAME"

if git rev-parse --verify "$BRANCH_NAME" 2>/dev/null; then
    echo "⚠️  Branch já existe. Checando out..."
    git checkout "$BRANCH_NAME"
else
    echo "✨ Criando novo branch..."
    git checkout -b "$BRANCH_NAME"
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 5: Validar mudanças de código
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 5: Validando código..."
echo ""

if [ -f "patch_client_credentials.diff" ]; then
    echo "📝 Patch encontrado: patch_client_credentials.diff"
    echo ""
    echo "Aplicar patch? (s/n)"
    read -r apply_patch

    if [[ "$apply_patch" == "s" || "$apply_patch" == "S" ]]; then
        echo "Aplicando patch..."
        patch -p1 < patch_client_credentials.diff || {
            echo "⚠️  Conflitos ao aplicar patch. Resolva manualmente e execute:"
            echo "  git add ."
            echo "  $0"
            exit 1
        }
        echo "✅ Patch aplicado"
    fi
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 6: Lint e Format
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 6: Formatando código..."
echo ""

echo "Executando: npm run lint"
npm run lint

echo ""
echo "✅ Código formatado"
echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 7: Build e Testes
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 7: Build e testes..."
echo ""

echo "Executando: ./build_and_test.sh"
./build_and_test.sh

echo ""
echo "✅ Build e testes passaram!"
echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 8: Commit
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 8: Preparando commit..."
echo ""

echo "Adicionando mudanças..."
git add -A

echo ""
echo "Verificando status:"
git status

echo ""
echo "Criar commit com a seguinte mensagem?"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
cat << 'COMMIT_MSG'
feat(sharepoint): add client credentials authentication flow

Implements OAuth2 Client Credentials flow for SharePoint authentication,
enabling service-to-service authentication without browser interaction.

Key changes:
- New class: SharePointClientCredentialsAuth
- Supports grant_type: client_credentials
- Tokens cached automatically by DuckDB secrets
- Backward compatible with existing Device Code Flow

Usage:
  CREATE SECRET sp_secret (
    TYPE sharepoint,
    PROVIDER config,
    CLIENT_ID '...',
    CLIENT_SECRET '...',
    TENANT_ID '...',
    AUTH_FLOW 'client_credentials'
  );

Closes #N/A
COMMIT_MSG
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "Confirmar commit? (s/n)"
read -r confirm_commit

if [[ "$confirm_commit" == "s" || "$confirm_commit" == "S" ]]; then
    git commit -m "feat(sharepoint): add client credentials authentication flow

Implements OAuth2 Client Credentials flow for SharePoint authentication,
enabling service-to-service authentication without browser interaction.

Key changes:
- New class: SharePointClientCredentialsAuth
- Supports grant_type: client_credentials
- Tokens cached automatically by DuckDB secrets
- Backward compatible with existing Device Code Flow

Usage:
  CREATE SECRET sp_secret (
    TYPE sharepoint,
    PROVIDER config,
    CLIENT_ID '...',
    CLIENT_SECRET '...',
    TENANT_ID '...',
    AUTH_FLOW 'client_credentials'
  );"

    echo "✅ Commit criado!"
else
    echo "❌ Commit cancelado"
    exit 1
fi

echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 9: Push
# ─────────────────────────────────────────────────────────────────

echo "📋 Passo 9: Push para GitHub..."
echo ""

echo "Push para: origin/$BRANCH_NAME"
git push origin "$BRANCH_NAME"

echo "✅ Push completo!"
echo ""

# ─────────────────────────────────────────────────────────────────
# PASSO 10: Instruções finais
# ─────────────────────────────────────────────────────────────────

echo "════════════════════════════════════════════════════════════"
echo "  ✨ Próximo Passo: Abrir Pull Request"
echo "════════════════════════════════════════════════════════════"
echo ""

PR_URL="https://github.com/trouchet/cloudfs/pull/new/$BRANCH_NAME"
echo "1️⃣  Abra o link abaixo:"
echo "   $PR_URL"
echo ""

echo "2️⃣  Use o template de PR:"
cat << 'PR_TEMPLATE'
## 📝 Descrição
Implementa suporte a OAuth2 Client Credentials flow para autenticação
no SharePoint. Permite autenticação service-to-service sem interação
do usuário, ideal para aplicações headless.

## 🎯 Tipo de Mudança
- [x] Nova feature
- [ ] Bug fix
- [ ] Breaking change
- [ ] Documentação

## 🧪 Testes
- [x] Testes unitários adicionados
- [x] Build passa em todas as plataformas
- [x] Compatibilidade com Device Code Flow verificada

## ✅ Checklist
- [x] Código segue o style guide
- [x] Build passa (./build_and_test.sh)
- [x] Commit message no formato correto
- [x] Mudanças backward-compatible

## 📋 Issues Relacionadas
Implementa suporte para autenticação headless do CloudFS
PR_TEMPLATE

echo ""
echo "3️⃣  Aguarde review dos maintainers"
echo ""
echo "💡 Dica: Você pode continuar trabalhando em outro branch enquanto"
echo "         aguarda o review."
echo ""
