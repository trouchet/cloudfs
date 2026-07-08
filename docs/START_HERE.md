# � CloudFS + SharePoint - Uso Rápido

Sua extensão foi **publicada**! Use assim:

## ⚡ Começar em 2 linhas

### Extensão Oficial (do Registry)

```sql
INSTALL cloudfs;
LOAD cloudfs;
```

### Compilada Localmente (use `-unsigned`)

```bash
export PATH="$HOME/bin:$PATH"
duckdb-cloudfs
```

```sql
LOAD cloudfs;
```

Pronto! 🎉

> **ℹ️ Primeira Execução**: CloudFS usa OAuth2 Device Code Flow. Na primeira vez
> que acessar SharePoint, você verá um código para digitar em um navegador.

______________________________________________________________________

## 🔐 Autenticação SharePoint (Important!)

CloudFS utiliza **OAuth2 Device Code Flow** para autenticação. Isso significa:

1. ✅ Você NÃO precisa configurar nenhum secret
1. ✅ Basta digitar um código de autorização na primeira vez
1. ✅ O token é salvo automaticamente

**Como funciona na prática:**

```bash
export PATH="$HOME/bin:$PATH"
duckdb-cloudfs
```

```sql
D LOAD cloudfs;
D SELECT * FROM ls('spfs://Concilium/prod/');
```

Quando você executar, verá algo como:

```
[cloudfs SharePoint] Authenticate at: https://microsoft.com/devicelogin
Code: ABC123XYZ
Waiting...
```

Então:

1. **Abra** o link em outro terminal
1. **Digite** o código `ABC123XYZ`
1. **Clique** em "Aprovar"
1. Volte ao DuckDB - funcionará! ✅

## **Depois disso:** O token é salvo, não precisa autenticar novamente

## 🔐 Configurar SharePoint (Opcional)

Se quiser usar SharePoint, defina as credenciais:

```bash
export CLOUDFS_SHAREPOINT_TENANT_ID='seu-tenant-id'
export CLOUDFS_SHAREPOINT_CLIENT_ID='seu-client-id'
export CLOUDFS_SHAREPOINT_CLIENT_SECRET='seu-client-secret'

duckdb
```

______________________________________________________________________

## 📊 Usar Imediatamente

```sql
-- Já instalou? Só carrega
LOAD cloudfs;

-- Use qualquer um dos protocolos
SELECT * FROM 'spfs://sites/seu-site/Shared Documents/dados.csv';
SELECT * FROM 'odfs://pasta/arquivo.parquet';
SELECT * FROM 'gdfs://My Drive/planilha.csv';
SELECT * FROM 'dbxfs:///pasta/arquivo.json';
```

______________________________________________________________________

## � Exemplos Prontos

### Listar arquivos do SharePoint

```sql
SELECT * FROM ls('spfs://sites/seu-site/Shared Documents/');
```

### Ler diferentes formatos

```sql
-- CSV
SELECT * FROM read_csv('spfs://sites/seu-site/Shared Documents/vendas.csv');

-- Parquet (recomendado para performance)
SELECT * FROM 'spfs://sites/seu-site/Shared Documents/relatorio.parquet';

-- JSON
SELECT * FROM read_json('spfs://sites/seu-site/Shared Documents/config.json');
```

### Escrever dados de volta

```sql
COPY (
    SELECT customer, SUM(amount) as total
    FROM vendas
    GROUP BY customer
) TO 'spfs://sites/seu-site/Shared Documents/resumo.parquet';
```

______________________________________________________________________

## 🎯 Todos os Protocolos

```
spfs://                 ← SharePoint
odfs://                 ← OneDrive  
gdfs://                 ← Google Drive
dbxfs://                ← Dropbox
sftp://                 ← SFTP
vfs://                  ← VFS Agent
```

______________________________________________________________________

## 🆘 Troubleshooting

### "HTTP Error 404" no INSTALL?

Pode ser que a extensão ainda esteja sendo processada. Aguarde alguns minutos e
tente novamente:

```sql
INSTALL cloudfs;
```

Se persistir, compile localmente:

```bash
./compile_for_current_duckdb.sh  # Auto-detecta sua versão do DuckDB
```

### "File was built for DuckDB vX.X.X, not vY.Y.Y"?

A extensão foi compilada para versão diferente. Use o script automático:

```bash
./compile_for_current_duckdb.sh
```

Ou compile manualmente:

```bash
git -C duckdb checkout v$(duckdb -version | awk '{print $2}')
rm -rf build
make release
```

### "signature is either missing or invalid"?

A extensão compilada localmente não é assinada. Use uma das opções:

**Opção 1** (Temporária):

```bash
duckdb -unsigned
LOAD cloudfs;
```

**Opção 2** (Permanente com script):

```bash
duckdb-cloudfs  # Já abre com -unsigned
```

**Opção 3** (Configuração global): Adicione a `~/.duckdbrc`:

```
.mode unsigned on
```

### "Authentication failed"?

Verifique se as variáveis de ambiente estão corretas:

```bash
echo $CLOUDFS_SHAREPOINT_TENANT_ID
echo $CLOUDFS_SHAREPOINT_CLIENT_ID
```

### "File not found"?

Liste o que existe:

```sql
SELECT * FROM ls('spfs://sites/');
```

______________________________________________________________________

## 📚 Mais Informações

- [README.md](README.md) - Visão geral completa
- [QUICKSTART_SHAREPOINT.md](QUICKSTART_SHAREPOINT.md) - Exemplos detalhados
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) - Desenvolvimento

______________________________________________________________________

**É isso! Bora usar?** 🚀
