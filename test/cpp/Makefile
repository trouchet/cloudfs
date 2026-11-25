# Standalone unit tests for helpers that do not depend on DuckDB.
#   make -C test/cpp
CXX ?= c++
ROOT := ../..
OUT := $(ROOT)/build/unit

.PHONY: test clean
test: $(OUT)/test_http_util
	$(OUT)/test_http_util

$(OUT)/test_http_util: test_http_util.cpp $(ROOT)/src/core/cloud_http.cpp $(ROOT)/src/include/core/cloud_http.hpp
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -Wall -Wextra -I$(ROOT)/src/include $< $(ROOT)/src/core/cloud_http.cpp -lcurl -o $@

clean:
	rm -rf $(OUT)
