#pragma once

#include "dudu/native_headers.hpp"

#include <filesystem>
#include <string>

namespace dudu {

struct NativeHeaderRawCache {
    bool hit = false;
    std::filesystem::path base;
    std::string ast_dump;
    std::string macro_dump;
};

NativeHeaderRawCache load_native_header_raw_cache(const NativeHeaderOptions& options,
                                                  const std::string& key);
void store_native_header_raw_cache(const NativeHeaderRawCache& cache,
                                   const std::string& ast_dump,
                                   const std::string& macro_dump);

} // namespace dudu
