# Standalone unit tests for helpers that do not depend on DuckDB.
#   make -C test/cpp
CXX ?= c++
ROOT := ../..
OUT := $(ROOT)/build/unit

BUILD := $(ROOT)/build/release

.PHONY: test upload oauth clean
test: $(OUT)/test_http_util $(OUT)/test_loopback
	$(OUT)/test_http_util
	$(OUT)/test_loopback

# cloud_http.cpp parses JSON with the yyjson copy vendored by DuckDB, which
# includes a few header-only DuckDB helpers.
YYJSON := $(ROOT)/duckdb/third_party/yyjson

$(OUT)/test_http_util: test_http_util.cpp $(ROOT)/src/core/cloud_http.cpp $(ROOT)/src/include/core/cloud_http.hpp
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -Wall -Wextra -I$(ROOT)/src/include -I$(YYJSON)/include \
		-I$(ROOT)/duckdb/src/include $< \
		$(ROOT)/src/core/cloud_http.cpp $(YYJSON)/yyjson.cpp -lcurl -o $@

$(OUT)/test_loopback: test_loopback.cpp $(ROOT)/src/core/cloud_loopback.cpp $(ROOT)/src/include/core/cloud_loopback.hpp
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -Wall -Wextra -I$(ROOT)/src/include $< \
		$(ROOT)/src/core/cloud_loopback.cpp -pthread -o $@

# The tests below need a finished release build: they link the static
# extension and libduckdb.
LINK_EXT := $(BUILD)/extension/cloudfs/libcloudfs_extension.a -L$(BUILD)/src -lduckdb \
	-lcurl -lssh2 -lssl -lcrypto -lz -Wl,-rpath,$(abspath $(BUILD)/src)
EXT_INCLUDES := -I$(ROOT)/duckdb/src/include -I$(ROOT)/src/include

upload: $(OUT)/test_upload
	$(OUT)/test_upload

oauth: $(OUT)/test_oauth_persistence $(OUT)/test_gdrive_oauth
	$(OUT)/test_oauth_persistence
	$(OUT)/test_gdrive_oauth

$(OUT)/test_%: test_%.cpp $(BUILD)/extension/cloudfs/libcloudfs_extension.a
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 $(EXT_INCLUDES) $< $(LINK_EXT) -o $@

clean:
	rm -rf $(OUT)
