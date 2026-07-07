#!/bin/bash
# Setup Unificado: Importa Secrets de Qualquer Fonte

set -e

echo "════════════════════════════════════════════════════════"
echo "  🔐 CloudFS: Setup de Secrets do SharePoint"
echo "════════════════════════════════════════════════════════"
echo ""
echo "Escolha a fonte dos secrets:"
echo ""
echo "  [1] 📁 Arquivo local (.env)"
echo "  [2] 🔑 Azure Key Vault"
echo "  [3] 🐙 GitHub Secrets (seu repo)"
echo "  [4] ⌨️  Digitar manualmente"
echo ""
read -p "Escolha [1-4]: " source_choice

case $source_choice in
  1)
    echo ""
    echo "📄 Criando .env local..."
    
    cat > ~/.env.sharepoint << 'EOF'
# SharePoint / OneDrive Credentials
# Configure com seus valores

export CLOUDFS_SHAREPOINT_TENANT_ID="seu-tenant-id"
export CLOUDFS_SHAREPOINT_CLIENT_ID="seu-client-id"
export CLOUDFS_SHAREPOINT_CLIENT_SECRET="seu-client-secret"
export CLOUDFS_SHAREPOINT_DRIVE_ID="seu-drive-id"
export CLOUDFS_SHAREPOINT_ROOT_PATH="Concilium/prod"
EOF
    
    echo "✅ Arquivo criado: ~/.env.sharepoint"
    echo ""
    echo "Edite agora:"
    nano ~/.env.sharepoint
    ;;
  
  2)
    echo ""
    echo "🔑 Importando do Azure Key Vault..."
    exec ./import_from_keyvault.sh
    ;;
  
  3)
    echo ""
    echo "🐙 Seus repositórios:"
    gh repo list --json nameWithOwner -q '.[].nameWithOwner' | head -10 | nl
    echo ""
    read -p "Digite o nome completo do repo (ex: usuario/repo): " repo_name
    
    echo ""
    echo "📋 Secrets em $repo_name:"
    SECRETS=$(gh secret list -R "$repo_name" --json name -q '.[].name' 2>/dev/null || true)
    
    if [ -z "$SECRETS" ]; then
      echo "❌ Sem acesso ou nenhum secret encontrado"
      exit 1
    fi
    
    echo "$SECRETS"
    echo ""
    echo "Mapeando SharePoint secrets..."
    
    declare -a SECRET_NAMES=()
    for secret in $SECRETS; do
      if [[ "$secret" =~ (SP|SHAREPOINT|ONEDRIVE|DRIVE) ]]; then
        echo "  ✓ $secret"
        SECRET_NAMES+=("$secret")
      fi
    done
    
    if [ ${#SECRET_NAMES[@]} -eq 0 ]; then
      echo "❌ Nenhum secret do SharePoint encontrado"
      exit 1
    fi
    
    echo ""
    echo "O que deseja fazer?"
    echo "  [a] Criar arquivo .env"
    echo "  [b] Enviar para GitHub (cloudfs repo)"
    echo ""
    read -p "Escolha [a/b]: " action_choice
    
    case $action_choice in
      a)
        echo ""
        echo "💾 Criando .env.sharepoint..."
        
        cat > ~/.env.sharepoint << 'EOF'
# SharePoint Configuration from GitHub Secrets
EOF
        
        for secret_name in "${SECRET_NAMES[@]}"; do
          value=$(gh secret view "$secret_name" -R "$repo_name" 2>/dev/null || true)
          if [ -n "$value" ]; then
            echo "export CLOUDFS_${secret_name}='$value'" >> ~/.env.sharepoint
          fi
        done
        
        echo "✅ Arquivo criado"
        ;;
      b)
        echo ""
        echo "📤 Enviando para cloudfs repo..."
        
        for secret_name in "${SECRET_NAMES[@]}"; do
          # Mapear nome
          if [[ "$secret_name" =~ TENANT ]]; then
            target="CLOUDFS_SHAREPOINT_TENANT_ID"
          elif [[ "$secret_name" =~ CLIENT_ID ]]; then
            target="CLOUDFS_SHAREPOINT_CLIENT_ID"
          elif [[ "$secret_name" =~ SECRET ]]; then
            target="CLOUDFS_SHAREPOINT_CLIENT_SECRET"
          elif [[ "$secret_name" =~ DRIVE ]]; then
            target="CLOUDFS_SHAREPOINT_DRIVE_ID"
          else
            target="CLOUDFS_${secret_name}"
          fi
          
          value=$(gh secret view "$secret_name" -R "$repo_name" 2>/dev/null || true)
          if [ -n "$value" ]; then
            echo -n "  $secret_name → $target ... "
            if gh secret set "$target" --body "$value" 2>/dev/null; then
              echo "✅"
            else
              echo "❌"
            fi
          fi
        done
        ;;
    esac
    ;;
  
  4)
    echo ""
    echo "⌨️  Digite os valores:"
    echo ""
    
    read -p "Tenant ID: " tenant_id
    read -p "Client ID: " client_id
    read -sp "Client Secret: " client_secret
    echo ""
    read -p "Drive ID (opcional): " drive_id
    echo ""
    
    echo ""
    echo "💾 Criando .env.sharepoint..."
    
    cat > ~/.env.sharepoint << EOF
export CLOUDFS_SHAREPOINT_TENANT_ID="$tenant_id"
export CLOUDFS_SHAREPOINT_CLIENT_ID="$client_id"
export CLOUDFS_SHAREPOINT_CLIENT_SECRET="$client_secret"
export CLOUDFS_SHAREPOINT_DRIVE_ID="$drive_id"
export CLOUDFS_SHAREPOINT_ROOT_PATH="Concilium/prod"
EOF
    
    echo "✅ Arquivo criado: ~/.env.sharepoint"
    ;;
  
  *)
    echo "❌ Opção inválida"
    exit 1
    ;;
esac

echo ""
echo "════════════════════════════════════════════════════════"
echo "✨ Setup Concluído!"
echo "════════════════════════════════════════════════════════"
echo ""
echo "🚀 Para usar:"
echo ""
echo "   1. Ativar ambiente:"
echo "      source ~/.env.sharepoint"
echo ""
echo "   2. Abrir DuckDB:"
echo "      duckdb-cloudfs"
echo ""
echo "   3. Carregar CloudFS:"
echo "      D LOAD cloudfs;"
echo ""
echo "   4. Testar SharePoint:"
echo "      D SELECT * FROM 'spfs://sites/';"
echo ""
