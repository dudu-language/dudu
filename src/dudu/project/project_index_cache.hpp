#pragma once

#include "dudu/project/project_index.hpp"

#include <compare>
#include <filesystem>
#include <map>
#include <string>

namespace dudu {

struct ProjectIndexCacheStats {
    size_t entries = 0;
    size_t hits = 0;
    size_t misses = 0;
    size_t stale_evictions = 0;
    size_t loads = 0;
};

class ProjectIndexCache {
  public:
    const ProjectIndex& get(ProjectIndexOptions options);
    ProjectIndexCacheStats stats() const;
    void clear();

  private:
    struct CacheKey {
        std::string entry_path;
        size_t source_hash = 0;
        std::string source_overrides_fingerprint;
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
    ProjectIndexCacheStats stats_;
};

} // namespace dudu
