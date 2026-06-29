#pragma once

#include "dudu/project_index.hpp"

#include <compare>
#include <filesystem>
#include <map>
#include <string>

namespace dudu {

class ProjectIndexCache {
  public:
    const ProjectIndex& get(ProjectIndexOptions options);
    void clear();

  private:
    struct CacheKey {
        std::string entry_path;
        size_t source_hash = 0;
        std::string source_dir;
        std::string config_fingerprint;
        std::string build_values_fingerprint;
        bool force_module_tree = false;
        bool allow_module_tree = false;
        bool include_native_headers = false;
        bool include_native_headers_in_merged_module = false;
        bool check_semantics = false;
        bool check_bodies = false;

        friend auto operator<=>(const CacheKey& lhs, const CacheKey& rhs) = default;
    };
    struct CacheEntry {
        ProjectIndex index;
    };

    static CacheKey key_for_options(const ProjectIndexOptions& options);

    std::map<CacheKey, CacheEntry> entries_;
};

} // namespace dudu
