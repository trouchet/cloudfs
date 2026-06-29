#pragma once
#define DUCKDB_EXTENSION_MAIN
#include "duckdb.hpp"

namespace duckdb {

class CloudFsExtension : public Extension {
  public:
    void Load(ExtensionLoader& loader) override;
    std::string Name() override;
    std::string Version() const override;
};

} // namespace duckdb
