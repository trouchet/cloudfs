# Standalone unit tests for helpers that do not depend on DuckDB.
#   make -C test/cpp
CXX ?= c++
ROOT := ../..
OUT := $(ROOT)/build/unit

BUILD := $(ROOT)/build/release

.PHONY: test upload clean
test: $(OUT)/test_http_util
	$(OUT)/test_http_util

$(OUT)/test_http_util: test_http_util.cpp $(ROOT)/src/core/cloud_http.cpp $(ROOT)/src/include/core/cloud_http.hpp
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -Wall -Wextra -I$(ROOT)/src/include $< $(ROOT)/src/core/cloud_http.cpp -lcurl -o $@

# Needs a finished release build (links the static extension and libduckdb).
upload: $(OUT)/test_upload
	$(OUT)/test_upload

$(OUT)/test_upload: test_upload.cpp $(BUILD)/extension/cloudfs/libcloudfs_extension.a
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -I$(ROOT)/duckdb/src/include -I$(ROOT)/src/include $< \
		$(BUILD)/extension/cloudfs/libcloudfs_extension.a -L$(BUILD)/src -lduckdb \
		-lcurl -lssh2 -lssl -lcrypto -lz -Wl,-rpath,$(abspath $(BUILD)/src) -o $@

clean:
	rm -rf $(OUT)
