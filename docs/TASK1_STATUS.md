# Task 1 - Table Functions Fix - Status Report

## ✅ Correções Aplicadas (2026-06-03 08:22)

Foram identificados e corrigidos 3 problemas críticos que impediam as table functions de funcionarem:

### Problema 1: Header sem declaração de SetCloudFS
**Arquivo**: `src/include/core/cloud_table_functions.hpp`
**Fix**: Adicionada declaração `void SetCloudFS(CloudFileSystem *cfs);`

### Problema 2: Assinatura incompatível entre header e implementação  
**Arquivo**: `src/include/core/cloud_table_functions.hpp`  
**Fix**: Mudado de `void RegisterCloudTableFunctions(ExtensionLoader &loader, CloudFileSystem *cfs);`  
Para: `void RegisterCloudTableFunctions(ExtensionLoader &loader);`

### Problema 3: Table functions registradas com nullptr
**Arquivo**: `src/core/cloud_table_functions.cpp` (linha ~399)  
**Fix**: Mudado de:
```cpp
void RegisterCloudTableFunctions(ExtensionLoader &loader) {
    loader.RegisterFunction(LsTableFunction(nullptr));
    loader.RegisterFunction(StatTableFunction(nullptr));
    loader.RegisterFunction(DuTableFunction(nullptr));
}
```
Para:
```cpp
void RegisterCloudTableFunctions(ExtensionLoader &loader) {
    loader.RegisterFunction(LsTableFunction(g_tf_cfs));
    loader.RegisterFunction(StatTableFunction(g_tf_cfs));
    loader.RegisterFunction(DuTableFunction(g_tf_cfs));
}
```

## 🔨 Status de Compilação

**Arquivos fonte modificados**: 08:22 (hoje)
**Binário existente**: 07:41 (desatualizado)

⚠️ **As mudanças precisam ser compiladas para testar**

## 📋 Comandos para Compilar e Testar

### Passo 1: Instalar dependências (se necessário)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
     libcurl4-openssl-dev libssl-dev libssh2-1-dev
```

### Passo 2: Compilar (assumindo que tem submodule duckdb/)
```bash
cd /home/pingu/github/cloudfs

# Se este projeto usa extension-template com Makefile
GEN=ninja make release

# OU se usa CMake direto
mkdir -p build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
ninja
cd ..
```

### Passo 3: Testar as table functions
```bash
cd /home/pingu/github/cloudfs

# Gerar token
TOKEN=$(openssl rand -hex 16)

# Criar dados de teste
mkdir -p /tmp/vfs_test/data
echo "test content" > /tmp/vfs_test/data/test.txt

# Iniciar cloudfs-agent
chmod +x ./cloudfs-agent
./cloudfs-agent --token "$TOKEN" --port 19876 --root /tmp/vfs_test &
AGENT_PID=$!
sleep 2

# Testar com DuckDB
duckdb -unsigned << EOF
-- Carregar extensão
LOAD './cloudfs.duckdb_extension';

-- Criar secret
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');

-- Teste 1: ls() deve listar arquivos
SELECT name, type, size_pretty FROM ls('vfs://localhost:19876/data/');

-- Teste 2: stat() deve retornar metadata de um arquivo
SELECT name, size, type FROM stat('vfs://localhost:19876/data/test.txt');

-- Teste 3: du() deve mostrar uso de disco
SELECT directory, file_count, size_pretty FROM du('vfs://localhost:19876/');
EOF

# Cleanup
kill $AGENT_PID
rm -rf /tmp/vfs_test
```

### Resultado Esperado

**ls() deve retornar:**
```
┌───────────┬──────────┬─────────────┐
│   name    │   type   │ size_pretty │
├───────────┼──────────┼─────────────┤
│ test.txt  │ file     │ 13 B        │
└───────────┴──────────┴─────────────┘
```

**stat() deve retornar:**
```
┌───────────┬──────┬──────────┐
│   name    │ size │   type   │
├───────────┼──────┼──────────┤
│ test.txt  │   13 │ file     │
└───────────┴──────┴──────────┘
```

**du() deve retornar:**
```
┌─────────────────────────────┬────────────┬─────────────┐
│         directory           │ file_count │ size_pretty │
├─────────────────────────────┼────────────┼─────────────┤
│ vfs://localhost:19876/      │          1 │ 13 B        │
└─────────────────────────────┴────────────┴─────────────┘
```

## 🔍 Verificar se as mudanças estão no código

```bash
cd /home/pingu/github/cloudfs

# Verificar SetCloudFS no header
grep -n "void SetCloudFS" src/include/core/cloud_table_functions.hpp

# Verificar assinatura RegisterCloudTableFunctions no header  
grep -n "void RegisterCloudTableFunctions" src/include/core/cloud_table_functions.hpp

# Verificar que usa g_tf_cfs e não nullptr
grep -A 4 "void RegisterCloudTableFunctions" src/core/cloud_table_functions.cpp | grep -E "LsTableFunction|StatTableFunction|DuTableFunction"
```

**Output esperado:**
```
54:void SetCloudFS(CloudFileSystem *cfs);
60:void RegisterCloudTableFunctions(ExtensionLoader &loader);

    loader.RegisterFunction(LsTableFunction(g_tf_cfs));
    loader.RegisterFunction(StatTableFunction(g_tf_cfs));
    loader.RegisterFunction(DuTableFunction(g_tf_cfs));
```

## ✅ Confirmação Visual

Se os greps acima retornarem os valores esperados, **as correções estão aplicadas corretamente**.

Resta apenas:
1. ✅ Código fonte corrigido 
2. ⏳ Compilar
3. ⏳ Testar

## 🚧 Bloqueadores Atuais

- **Ambiente de build não configurado** (precisa cmake, ninja, toolchain)
- **Sem sudo access** para instalar dependências
- **Binário desatualizado** (compilado antes das correções)

## 📝 Próximos Passos Recomendados

1. **Instalar build tools** (requer sudo):
   ```bash
   sudo apt-get install -y build-essential cmake ninja-build \
        libcurl4-openssl-dev libssl-dev libssh2-1-dev
   ```

2. **Verificar estrutura do projeto**:
   - Se há submodule `duckdb/` → usar `make release`
   - Se não → precisará clonar projeto corretamente com `--recursive`

3. **Compilar** com o comando adequado

4. **Testar** com o script de teste fornecido

## 🔗 Referências

- **Documentação principal**: AGENT_HANDOVER.md (Task 1)
- **Arquivos modificados**:
  - `src/include/core/cloud_table_functions.hpp`
  - `src/core/cloud_table_functions.cpp`
- **Workflow de build**: Requer extension-template do DuckDB
