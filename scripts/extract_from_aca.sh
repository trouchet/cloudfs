#!/bin/bash
# Extrai variáveis do SharePoint da ACA concilium-api

set -e

echo "════════════════════════════════════════════════════════"
echo "  🚀 Extraindo Secrets da ACA concilium-api"
echo "════════════════════════════════════════════════════════"
echo ""

RESOURCE_GROUP="concilium-rg"
ACA_NAME="concilium-api"

echo "✅ Conectando à ACA: $ACA_NAME..."
echo ""

# Extrair variáveis
echo "📋 Extraindo variáveis de ambiente..."

# Extrair valores da ACA
TENANT_ID=$(az containerapp show --name "$ACA_NAME" --resource-group "$RESOURCE_GROUP" --output json 2>/dev/null | \
  jq -r '.properties.template.containers[0].env[] | select(.name=="CONCILIUM__SP_TENANT_ID") | .value')

CLIENT_ID=$(az containerapp show --name "$ACA_NAME" --resource-group "$RESOURCE_GROUP" --output json 2>/dev/null | \
  jq -r '.properties.template.containers[0].env[] | select(.name=="CONCILIUM__SP_CLIENT_ID") | .value')

CLIENT_SECRET=$(az containerapp show --name "$ACA_NAME" --resource-group "$RESOURCE_GROUP" --output json 2>/dev/null | \
  jq -r '.properties.template.containers[0].env[] | select(.name=="CONCILIUM__SP_CLIENT_SECRET") | .value')

DRIVE_ID=$(az containerapp show --name "$ACA_NAME" --resource-group "$RESOURCE_GROUP" --output json 2>/dev/null | \
  jq -r '.properties.template.containers[0].env[] | select(.name=="CONCILIUM__SP_DRIVE_ID") | .value')

ROOT_PATH=$(az containerapp show --name "$ACA_NAME" --resource-group "$RESOURCE_GROUP" --output json 2>/dev/null | \
  jq -r '.properties.template.containers[0].env[] | select(.name=="CONCILIUM__SP_ROOT_PATH") | .value')

# Validar
if [ -z "$TENANT_ID" ] || [ "$TENANT_ID" = "null" ]; then
  echo "❌ Não consegui extrair variáveis da ACA"
  echo ""
  echo "Verifique:"
  echo "  az account show"
  echo "  az containerapp list"
  exit 1
fi

echo "✅ Variáveis encontradas:"
echo "  • TENANT_ID: ${TENANT_ID:0:8}...${TENANT_ID: -8}"
echo "  • CLIENT_ID: ${CLIENT_ID:0:8}...${CLIENT_ID: -8}"
echo "  • CLIENT_SECRET: ${CLIENT_SECRET:0:8}...${CLIENT_SECRET: -8}"
echo "  • DRIVE_ID: ${DRIVE_ID:0:8}...${DRIVE_ID: -8}"
echo "  • ROOT_PATH: $ROOT_PATH"
echo ""

echo "O que deseja fazer?"
echo ""
echo "  [1] 💾 Salvar em ~/.env.sharepoint"
echo "  [2] 📤 Enviar para GitHub Secrets"
echo "  [3] 👁️  Apenas visualizar"
echo ""
read -p "Escolha [1-3]: " choice

case $choice in
  1)
    echo ""
    echo "💾 Criando ~/.env.sharepoint..."

    cat > ~/.env.sharepoint << ENVEOF
# SharePoint Configuration from ACA concilium-api
# Extraído em $(date)

export CLOUDFS_SHAREPOINT_TENANT_ID="$TENANT_ID"
export CLOUDFS_SHAREPOINT_CLIENT_ID="$CLIENT_ID"
export CLOUDFS_SHAREPOINT_CLIENT_SECRET="$CLIENT_SECRET"
export CLOUDFS_SHAREPOINT_DRIVE_ID="$DRIVE_ID"
export CLOUDFS_SHAREPOINT_ROOT_PATH="$ROOT_PATH"
ENVEOF

    chmod 600 ~/.env.sharepoint
    echo "✅ Arquivo criado: ~/.env.sharepoint (permissões: 600)"
    echo ""
    echo "Use com:"
    echo "  source ~/.env.sharepoint"
    echo "  duckdb-cloudfs"
    ;;

  2)
    echo ""
    echo "📤 Enviando para GitHub Secrets..."

    echo -n "  CLOUDFS_SHAREPOINT_TENANT_ID... "
    gh secret set CLOUDFS_SHAREPOINT_TENANT_ID --body "$TENANT_ID" && echo "✅" || echo "❌"

    echo -n "  CLOUDFS_SHAREPOINT_CLIENT_ID... "
    gh secret set CLOUDFS_SHAREPOINT_CLIENT_ID --body "$CLIENT_ID" && echo "✅" || echo "❌"

    echo -n "  CLOUDFS_SHAREPOINT_CLIENT_SECRET... "
    gh secret set CLOUDFS_SHAREPOINT_CLIENT_SECRET --body "$CLIENT_SECRET" && echo "✅" || echo "❌"

    echo -n "  CLOUDFS_SHAREPOINT_DRIVE_ID... "
    gh secret set CLOUDFS_SHAREPOINT_DRIVE_ID --body "$DRIVE_ID" && echo "✅" || echo "❌"

    echo -n "  CLOUDFS_SHAREPOINT_ROOT_PATH... "
    gh variable set CLOUDFS_SHAREPOINT_ROOT_PATH --body "$ROOT_PATH" && echo "✅" || echo "❌"

    echo ""
    echo "✅ Secrets enviados para GitHub!"
    ;;

  3)
    echo ""
    echo "👁️  Valores:"
    echo ""
    echo "  TENANT_ID:"
    echo "    $TENANT_ID"
    echo ""
    echo "  CLIENT_ID:"
    echo "    $CLIENT_ID"
    echo ""
    echo "  CLIENT_SECRET:"
    echo "    $CLIENT_SECRET"
    echo ""
    echo "  DRIVE_ID:"
    echo "    $DRIVE_ID"
    echo ""
    echo "  ROOT_PATH:"
    echo "    $ROOT_PATH"
    ;;

  *)
    echo "❌ Opção inválida"
    exit 1
    ;;
esac

echo ""
echo "════════════════════════════════════════════════════════"
echo "✨ Concluído!"
echo "════════════════════════════════════════════════════════"
echo ""
echo "🚀 Próximos passos:"
echo ""
echo "  1. source ~/.env.sharepoint"
echo "  2. duckdb-cloudfs"
echo "  3. D LOAD cloudfs;"
echo "  4. D SELECT * FROM 'spfs://sites/';"
echo ""
