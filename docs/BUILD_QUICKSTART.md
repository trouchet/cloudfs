# 🚀 Quick Start - Build & Test CloudFS

## ⚠️ Problema Identificado

O binário `cloudfs.duckdb_extension` foi compilado para DuckDB v0.0.1, mas você tem v1.4.0 instalado.
**Solução**: Recompilar a extensão.

---

## 📦 Passo 1: Instalar Dependências

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
     libssl-dev libcurl4-openssl-dev libssh2-1-dev
```

**Nota**: Se o sudo estiver desabilitado no Windows, vá em **Settings → Developer Settings** para habilitá-lo.

---

## 🔨 Passo 2: Verificar Dependências (Opcional)

```bash
chmod +x check_deps.sh
./check_deps.sh
```

Se tudo estiver OK, prossiga para o Passo 3.

---

## 🏗️ Passo 3: Compilar

### Opção A: Script Automático (Recomendado)

```bash
chmod +x build_and_test.sh
./build_and_test.sh
```

Este script:
- ✅ Compila a extensão
- ✅ Testa todas as table functions (ls, stat, du)
- ✅ Verifica leitura de arquivos via vfs://

### Opção B: Manual

```bash
# Limpar build anterior
rm -rf build
mkdir build && cd build

# Configurar CMake
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release

# Compilar
ninja

# Copiar extensão para raiz do projeto
cp cloudfs.duckdb_extension ../
cd ..
```

---

## ✅ Passo 4: Teste Rápido

Após compilar com sucesso:

```bash
# Gerar token
TOKEN=$(openssl rand -hex 16)

# Criar dados de teste
mkdir -p /tmp/test_cloudfs/data
echo "hello world" > /tmp/test_cloudfs/data/sample.txt

# Iniciar agent
./cloudfs-agent --token "$TOKEN" --port 19876 --root /tmp/test_cloudfs &

# Testar
duckdb -unsigned << EOF
LOAD './cloudfs.duckdb_extension';
CREATE SECRET s (TYPE vfs, PROVIDER token, TOKEN '$TOKEN');

-- Teste 1: Listar arquivos
SELECT * FROM ls('vfs://localhost:19876/data/');

-- Teste 2: Metadata de arquivo
SELECT * FROM stat('vfs://localhost:19876/data/sample.txt');

-- Teste 3: Uso de disco
SELECT * FROM du('vfs://localhost:19876/');
EOF

# Cleanup
pkill cloudfs-agent
rm -rf /tmp/test_cloudfs
```

---

## 🎯 Resultado Esperado

Se a Task 1 foi corrigida com sucesso, você verá:

### ls() - Lista de arquivos
```
┌─────────────┬──────┬─────────────┬──────┬───────────────┐
│     url     │ name │    type     │ size │  size_pretty  │
├─────────────┼──────┼─────────────┼──────┼───────────────┤
│ vfs://...   │ samp…│ file        │   12 │ 12 B          │
└─────────────┴──────┴─────────────┴──────┴───────────────┘
```

### stat() - Metadata
```
┌────────────┬──────┬──────┬─────────────┐
│    name    │ size │ type │   modified  │
├────────────┼──────┼──────┼─────────────┤
│ sample.txt │   12 │ file │ 2026-06-03… │
└────────────┴──────┴──────┴─────────────┘
```

### du() - Uso de disco
```
┌─────────────────────────┬────────────┬─────────────┐
│       directory         │ file_count │ size_pretty │
├─────────────────────────┼────────────┼─────────────┤
│ vfs://localhost:19876/  │          1 │ 12 B        │
└─────────────────────────┴────────────┴─────────────┘
```

---

## 🐛 Troubleshooting

### Erro: "Could NOT find OpenSSL"
```bash
sudo apt-get install -y libssl-dev libcurl4-openssl-dev libssh2-1-dev
```

### Erro: "cmake: command not found"
```bash
sudo apt-get install -y cmake ninja-build
```

### Erro: "The file was built for DuckDB version 'v0.0.1'"
- Você está usando o binário antigo
- Solução: Recompile seguindo o Passo 3

### Erro: "Secret provider 'token' not found for type 'vfs'"
- A extensão não carregou corretamente
- Verifique se o caminho está correto: `LOAD './cloudfs.duckdb_extension';`
- Certifique-se de que recompilou após as correções

### Erro: "Table Function with name ls does not exist"
- As table functions não foram registradas
- Isso significa que o binário é antigo OU as correções não foram compiladas
- Recompile e teste novamente

---

## 📋 Checklist

Antes de reportar problemas:

- [ ] Instalei todas as dependências (libssl-dev, libcurl4, libssh2)
- [ ] Recompilei a extensão APÓS as correções (timestamp do binário > 08:22)
- [ ] Verifiquei que `cloudfs-agent` está rodando (`ps aux | grep cloudfs-agent`)
- [ ] Usei o token correto no secret
- [ ] O arquivo `cloudfs.duckdb_extension` está no diretório atual

---

## ✨ Status das Correções

**Data**: 2026-06-03 08:22  
**Arquivos corrigidos**:
- `src/include/core/cloud_table_functions.hpp` ✅
- `src/core/cloud_table_functions.cpp` ✅

**Próximo passo**: Recompilar e testar!
