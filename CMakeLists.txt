cmake_minimum_required(VERSION 3.5)
set(TARGET_NAME cloudfs)
set(CMAKE_CXX_STANDARD
    "17"
    CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${TARGET_NAME})

# Ubuntu multiarch puts libs in /usr/lib/x86_64-linux-gnu — cmake won't find
# them unless we add that path explicitly.
list(APPEND CMAKE_LIBRARY_PATH /usr/lib/x86_64-linux-gnu)
list(APPEND CMAKE_INCLUDE_PATH /usr/include/x86_64-linux-gnu)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Find libssh2 without requiring pkg-config
find_library(
  LIBSSH2_LIBRARIES
  NAMES ssh2
  PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib REQUIRED)
find_path(
  LIBSSH2_INCLUDE_DIRS
  NAMES libssh2.h
  PATHS /usr/include /usr/local/include REQUIRED)

# DuckDB: headers + static library. The static library is linked INTO the
# extension (making it self-contained). libduckdb_static.a + headers from the
# official release package at /tmp/libduckdb/.
if(NOT DUCKDB_INCLUDE_DIR)
  set(DUCKDB_INCLUDE_DIR "/tmp/duckdb-headers/src/include")
endif()
if(NOT DUCKDB_STATIC_LIB)
  set(DUCKDB_STATIC_LIB "/tmp/libduckdb/libduckdb_static.a")
endif()

set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

include_directories(src/include ${DUCKDB_INCLUDE_DIR})

# ── Core sources ──────────────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/cloud_cache.cpp src/core/cloud_http.cpp src/core/cloud_auth.cpp
    src/core/cloud_filesystem.cpp src/core/cloud_secret.cpp)

# ── Provider sources ──────────────────────────────────────────────────────────
set(PROVIDER_SOURCES
    src/providers/sharepoint/sharepoint_backend.cpp
    src/providers/sharepoint/sharepoint_auth.cpp
    src/providers/onedrive/onedrive_backend.cpp
    src/providers/onedrive/onedrive_auth.cpp
    src/providers/gdrive/gdrive_backend.cpp
    src/providers/gdrive/gdrive_auth.cpp
    src/providers/dropbox/dropbox_backend.cpp
    src/providers/dropbox/dropbox_auth.cpp
    src/providers/sftp/sftp_backend.cpp
    src/providers/vfs/vfs_backend.cpp
    src/core/cloud_table_functions.cpp)

# ── Extension entry point ─────────────────────────────────────────────────────
set(EXTENSION_SOURCES src/extension/cloudfs_extension.cpp ${CORE_SOURCES}
                      ${PROVIDER_SOURCES})

# ── Loadable extension (.duckdb_extension shared library) ─────────────────────
add_library(${LOADABLE_EXTENSION_NAME} SHARED ${EXTENSION_SOURCES})

target_link_libraries(
  ${LOADABLE_EXTENSION_NAME}
  ${DUCKDB_STATIC_LIB}
  OpenSSL::SSL
  OpenSSL::Crypto
  CURL::libcurl
  ${LIBSSH2_LIBRARIES}
  pthread
  dl)
target_include_directories(${LOADABLE_EXTENSION_NAME}
                           PRIVATE ${LIBSSH2_INCLUDE_DIRS})

# DuckDB headers define some constants inline in every TU that includes them,
# causing duplicate-definition errors when also linking against
# libduckdb_static. Both definitions are identical so taking the first one is
# safe.
target_link_options(${LOADABLE_EXTENSION_NAME} PRIVATE
                    -Wl,--allow-multiple-definition)

set_target_properties(
  ${LOADABLE_EXTENSION_NAME}
  PROPERTIES OUTPUT_NAME ${TARGET_NAME}
             PREFIX ""
             SUFFIX ".duckdb_extension")

install(TARGETS ${LOADABLE_EXTENSION_NAME}
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
