#pragma once

#include "dudu/native_headers.hpp"

#include <filesystem>
#include <optional>
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
void store_native_header_raw_cache(const NativeHeaderRawCache& cache, const std::string& ast_dump,
                                   const std::string& macro_dump);
std::optional<NativeHeaderScan> load_native_header_scan_cache(const NativeHeaderRawCache& cache,
                                                              const SourceLocation& location);
void store_native_header_scan_cache(const NativeHeaderRawCache& cache,
                                    const NativeHeaderScan& scan);
std::filesystem::path native_header_cache_dir(const NativeHeaderOptions& options);
std::filesystem::path clean_native_header_cache(const NativeHeaderOptions& options);

} // namespace dudu
