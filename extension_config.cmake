# This file is included by DuckDB's build system. It specifies which extension
# to load

# The version is set here rather than detected from git: DuckDB's detection
# reports the commit hash even on a release tag.  Keep it in step with
# description.yml, vcpkg.json and agent/main.go when releasing.
duckdb_extension_load(cloudfs SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
                      EXTENSION_VERSION v0.2.0)
