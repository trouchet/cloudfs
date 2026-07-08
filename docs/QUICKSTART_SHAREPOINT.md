# CloudFS + SharePoint - Guia Rápido 🚀

## 1️⃣ Instalar a Extensão

### Opção A: Build local (Recomendado para desenvolvimento)

```bash
cd /home/pingu/github/cloudfs
make release

# Criar diretório de extensões
mkdir -p ~/.duckdb/extensions/v1.5.1/linux_amd64/

# Copiar extensão compilada
cp build/release/repository/cloudfs.duckdb_extension ~/.duckdb/extensions/v1.5.1/linux_amd64/
```

### Opção B: Usar a extensão do repositório (quando disponível)

```bash
duckdb
> INSTALL cloudfs FROM repository;
> LOAD cloudfs;
```

______________________________________________________________________

## 2️⃣ Configurar SharePoint

### Criar Secret com Credenciais

```sql
-- No DuckDB
LOAD cloudfs;

-- Criar secret do SharePoint
CREATE SECRET sharepoint_secret (
    TYPE sharepoint,
    PROVIDER config,
    TENANT_ID 'seu-tenant-id',
    CLIENT_ID 'seu-client-id',
    CLIENT_SECRET 'seu-client-secret'
);

-- ⚠️ IMPORTANTE: Não use CREATE SECRET em produção!
-- Em produção, use variáveis de ambiente:
-- CLOUDFS_SHAREPOINT_TENANT_ID
-- CLOUDFS_SHAREPOINT_CLIENT_ID  
-- CLOUDFS_SHAREPOINT_CLIENT_SECRET
```

______________________________________________________________________

## 3️⃣ Usar Protocolos SharePoint

### Listar Arquivos

```sql
-- Listar arquivos em um site SharePoint
SELECT * FROM ls('spfs://sites/team-name/Shared Documents/');

-- Com filtro
SELECT * FROM ls('spfs://sites/team-name/Shared Documents/*.csv');
```

### Ler Dados Diretamente

```sql
-- Ler um Parquet
SELECT * FROM 'spfs://sites/team-name/Shared Documents/data.parquet';

-- Ler um CSV
SELECT * FROM read_csv('spfs://sites/team-name/Shared Documents/report.csv');

-- Ler um JSON
SELECT * FROM read_json('spfs://sites/team-name/Shared Documents/data.json');
```

### Informações de Arquivo

```sql
-- Metadados de arquivo
SELECT name, size, modified FROM stat('spfs://sites/team-name/Shared Documents/file.csv');

-- Uso de diretório
SELECT * FROM du('spfs://sites/team-name/Shared Documents/');
```

### Escrever Dados

```sql
-- Exportar para SharePoint
COPY (
    SELECT * FROM my_table
) TO 'spfs://sites/team-name/Shared Documents/output.parquet' (FORMAT PARQUET);
```

______________________________________________________________________

## 4️⃣ Exemplos Práticos

### Exemplo 1: Ler múltiplos CSVs do SharePoint

```sql
LOAD cloudfs;

-- Criar tabela a partir de múltiplos arquivos
CREATE TABLE consolidado AS
SELECT * FROM read_csv('spfs://sites/marketing/Shared Documents/*.csv');

-- Análise
SELECT COUNT(*) as total, COUNT(DISTINCT id) as unique_ids FROM consolidado;
```

### Exemplo 2: Cópia entre SharePoint e OneDrive

```sql
-- Copiar de SharePoint para OneDrive
COPY (
    SELECT * FROM 'spfs://sites/finance/Reports/sales.parquet'
) TO 'odfs://Reports/sales_backup.parquet' (FORMAT PARQUET);
```

### Exemplo 3: Query com cache inteligente

```sql
-- Primeira query: busca do SharePoint + cache
SELECT customer_id, SUM(amount) as total
FROM 'spfs://sites/sales/Data/transactions.parquet'
GROUP BY customer_id
ORDER BY total DESC
LIMIT 10;

-- Próximas queries: usa cache (mais rápido!)
SELECT * FROM 'spfs://sites/sales/Data/transactions.parquet'
WHERE customer_id = 123;
```

______________________________________________________________________

## 🔧 Troubleshooting

### Erro: "Extension not found"

```bash
# Verifique o diretório correto
ls -la ~/.duckdb/extensions/v1.5.1/linux_amd64/

# Copie novamente
cp build/release/repository/cloudfs.duckdb_extension ~/.duckdb/extensions/v1.5.1/linux_amd64/
```

### Erro: "Authentication failed"

```bash
# Verifique as credenciais
echo $CLOUDFS_SHAREPOINT_TENANT_ID
echo $CLOUDFS_SHAREPOINT_CLIENT_ID

# Teste com CREATE SECRET em vez de variáveis de ambiente
CREATE SECRET test_secret (
    TYPE sharepoint,
    PROVIDER config,
    TENANT_ID 'seu-tenant',
    CLIENT_ID 'seu-id',
    CLIENT_SECRET 'seu-secret'
);
```

### Erro: "File not found"

```bash
# Liste os arquivos disponíveis
SELECT * FROM ls('spfs://sites/your-site/');

# Verifique o caminho
SELECT * FROM stat('spfs://sites/your-site/Shared Documents/');
```

______________________________________________________________________

## 📚 Referências Rápidas

| Função           | Uso                    |
| ---------------- | ---------------------- |
| `ls()`           | Listar arquivos        |
| `stat()`         | Metadados de arquivo   |
| `du()`           | Uso de espaço em disco |
| `read_csv()`     | Ler CSV                |
| `read_json()`    | Ler JSON               |
| `read_parquet()` | Ler Parquet            |
| `COPY ... TO`    | Exportar dados         |

______________________________________________________________________

## 🎯 Próximos Passos

1. ✅ Compilar extensão
1. ✅ Instalar em DuckDB
1. ✅ Configurar autenticação SharePoint
1. ✅ Testar queries
1. 📖 Ver [DEVELOPMENT.md](docs/DEVELOPMENT.md) para mais detalhes
