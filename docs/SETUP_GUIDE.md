# 🚀 CloudFS + SharePoint - Setup Completo

Sua extensão CloudFS foi publicada! Aqui está o guia **rápido e fácil** para
usar com SharePoint.

## 📦 Arquivos de Setup Criados

- **`install.sh`** - Script de instalação automática (recomendado)
- **`setup_helper.py`** - Assistente interativo em Python
- **`QUICKSTART_SHAREPOINT.md`** - Guia completo com exemplos
- **`examples_sharepoint.sql`** - Queries prontas para usar

## ⚡ Instalação Rápida (3 passos)

### 1️⃣ Compilar e Instalar

```bash
cd /home/pingu/github/cloudfs
./install.sh
```

**Ou manualmente:**

```bash
# Compilar
make release

# Instalar
mkdir -p ~/.duckdb/extensions/v1.5.1/linux_amd64/
cp build/release/repository/cloudfs.duckdb_extension ~/.duckdb/extensions/v1.5.1/linux_amd64/
```

### 2️⃣ Configurar Credenciais do SharePoint

```bash
# Defina as variáveis de ambiente
export CLOUDFS_SHAREPOINT_TENANT_ID='seu-tenant-id'
export CLOUDFS_SHAREPOINT_CLIENT_ID='seu-app-id'
export CLOUDFS_SHAREPOINT_CLIENT_SECRET='seu-app-secret'
```

### 3️⃣ Usar no DuckDB

```bash
duckdb
```

```sql
-- Carregar extensão
LOAD cloudfs;

-- Listar seus sites
SELECT * FROM ls('spfs://sites/');

-- Ler um arquivo CSV do SharePoint
SELECT * FROM read_csv('spfs://sites/seu-site/Shared Documents/dados.csv');

-- Ler um Parquet
SELECT * FROM 'spfs://sites/seu-site/Shared Documents/report.parquet';
```

______________________________________________________________________

## 🔐 Configuração de Autenticação

### Opção A: Variáveis de Ambiente (Recomendado para Produção)

```bash
export CLOUDFS_SHAREPOINT_TENANT_ID='seu-tenant'
export CLOUDFS_SHAREPOINT_CLIENT_ID='seu-id'
export CLOUDFS_SHAREPOINT_CLIENT_SECRET='seu-secret'

duckdb
```

### Opção B: CREATE SECRET (Apenas Desenvolvimento!)

```sql
CREATE SECRET sharepoint_secret (
    TYPE sharepoint,
    PROVIDER config,
    TENANT_ID 'seu-tenant-id',
    CLIENT_ID 'seu-app-id',
    CLIENT_SECRET 'seu-app-secret'
);
```

______________________________________________________________________

## 📊 Exemplos de Uso

### Ler dados do SharePoint

```sql
LOAD cloudfs;

-- CSV
SELECT * FROM read_csv('spfs://sites/vendas/Shared Documents/202406.csv');

-- Parquet
SELECT * FROM 'spfs://sites/vendas/Shared Documents/dados.parquet' LIMIT 10;

-- JSON
SELECT * FROM read_json('spfs://sites/vendas/Shared Documents/config.json');
```

### Análise com cache automático

```sql
-- Primeira query: busca do SharePoint + cache
SELECT COUNT(*) as total, AVG(valor) as media
FROM 'spfs://sites/financeiro/Shared Documents/transactions.parquet'
WHERE data >= '2024-01-01';

-- Próximas queries usam cache (muito mais rápido!)
SELECT * FROM 'spfs://sites/financeiro/Shared Documents/transactions.parquet'
WHERE id = 12345;
```

### Exportar para SharePoint

```sql
-- Criar resultado e enviar para SharePoint
COPY (
    SELECT customer, SUM(amount) as total
    FROM my_table
    GROUP BY customer
    ORDER BY total DESC
) TO 'spfs://sites/relatorios/Shared Documents/resumo.parquet' (FORMAT PARQUET);
```

### Cópia entre provedores

```sql
-- OneDrive → SharePoint
COPY (
    SELECT * FROM 'odfs://Backup/dados.parquet'
) TO 'spfs://sites/dados/Shared Documents/dados_copia.parquet' (FORMAT PARQUET);
```

______________________________________________________________________

## 🛠️ Troubleshooting

### ❌ Erro: "Extension not found"

```bash
# Verificar se a extensão foi instalada
ls -la ~/.duckdb/extensions/v1.5.1/linux_amd64/

# Tentar reinstalar
./install.sh
```

### ❌ Erro: "Authentication failed"

```bash
# Verificar credenciais
echo $CLOUDFS_SHAREPOINT_TENANT_ID
echo $CLOUDFS_SHAREPOINT_CLIENT_ID

# Testar com credenciais hardcoded (apenas teste!)
CREATE SECRET test (
    TYPE sharepoint,
    PROVIDER config,
    TENANT_ID 'seu-tenant',
    CLIENT_ID 'seu-id',
    CLIENT_SECRET 'seu-secret'
);
```

### ❌ Erro: "File not found"

```sql
-- Listar o que existe
SELECT * FROM ls('spfs://sites/seu-site/');

-- Listar com recurso
SELECT * FROM ls('spfs://sites/seu-site/Shared Documents/');

-- Verificar metadados
SELECT * FROM stat('spfs://sites/seu-site/Shared Documents/seu-arquivo.csv');
```

### ❌ Performance lenta na primeira query

```sql
-- Isso é normal! A extensão:
-- 1. Conecta ao SharePoint
-- 2. Busca o arquivo
-- 3. Cacheia localmente
-- 4. Processa dados

-- Próximas queries são muito rápidas (cache de 3 níveis)
```

______________________________________________________________________

## 📚 Funcionalidades

| Recurso         | Status | Descrição        |
| --------------- | ------ | ---------------- |
| Listar arquivos | ✅     | `ls()`           |
| Metadados       | ✅     | `stat()`         |
| Espaço em disco | ✅     | `du()`           |
| Ler CSV         | ✅     | `read_csv()`     |
| Ler JSON        | ✅     | `read_json()`    |
| Ler Parquet     | ✅     | `read_parquet()` |
| Escrever dados  | ✅     | `COPY ... TO`    |
| Delta Lake      | ✅     | Suportado        |
| Iceberg         | ✅     | Suportado        |
| Cache LRU       | ✅     | 3 níveis         |
| OAuth2          | ✅     | Automático       |

______________________________________________________________________

## 🔗 Protocolos Suportados

```
spfs://sites/site-name/Shared Documents/file.csv     ← SharePoint
odfs://folder/file.parquet                            ← OneDrive
gdfs://My Drive/spreadsheet.csv                       ← Google Drive
dbxfs:///path/to/file.parquet                         ← Dropbox
sftp://user@host/path/file.csv                        ← SFTP
vfs://localhost:19876/data/file.json                  ← VFS Agent
```

______________________________________________________________________

## 💡 Dicas de Performance

1. **Use Parquet ao invés de CSV** para arquivos grandes
1. **Filtre dentro da query** ao invés de depois: `WHERE date > '2024-01-01'`
1. **O cache é automático** - próximas queries são rápidas
1. **Use `LIMIT` no início** para testar antes de processar tudo
1. **Combine múltiplos arquivos** com `glob patterns`:
   `spfs://sites/*/Shared Documents/*.parquet`

______________________________________________________________________

## 🆘 Suporte

Veja mais em:

- [`QUICKSTART_SHAREPOINT.md`](QUICKSTART_SHAREPOINT.md) - Guia completo
- [`DEVELOPMENT.md`](docs/DEVELOPMENT.md) - Desenvolvimento
- [`README.md`](README.md) - Visão geral da extensão

______________________________________________________________________

## 🎯 Próximos Passos

1. ✅ Instalar extensão: `./install.sh`
1. ✅ Configurar credenciais SharePoint
1. ✅ Testar primeira query
1. ✅ Usar em seu projeto DuckDB!

**Bom trabalho!** 🚀
