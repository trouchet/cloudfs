cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME cloudfs)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD
    "17"
    CACHE STRING "C++ standard to enforce" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

find_library(LIBSSH2_LIBRARIES NAMES ssh2 libssh2)
find_path(LIBSSH2_INCLUDE_DIRS NAMES libssh2.h)

include_directories(src/include)

set(CORE_SOURCES
    src/core/cloud_cache.cpp src/core/cloud_http.cpp src/core/cloud_auth.cpp
    src/core/cloud_filesystem.cpp src/core/cloud_secret.cpp
    src/core/cloud_table_functions.cpp)

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
    src/providers/vfs/vfs_backend.cpp)

set(EXTENSION_SOURCES src/extension/cloudfs_extension.cpp ${CORE_SOURCES}
                      ${PROVIDER_SOURCES})

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

set(EXTENSION_LINK_LIBS OpenSSL::SSL OpenSSL::Crypto CURL::libcurl
                        ${LIBSSH2_LIBRARIES})

target_link_libraries(${EXTENSION_NAME} ${EXTENSION_LINK_LIBS})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${EXTENSION_LINK_LIBS})

target_include_directories(${EXTENSION_NAME} PRIVATE ${LIBSSH2_INCLUDE_DIRS})
target_include_directories(${LOADABLE_EXTENSION_NAME}
                           PRIVATE ${LIBSSH2_INCLUDE_DIRS})

set_target_properties(${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME}
                      PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
