#!/bin/bash
# Abre DuckDB com CloudFS (unsigned mode ativado automaticamente)

exec duckdb -unsigned "$@"
