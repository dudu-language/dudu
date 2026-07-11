#include "dudu/project/project_index_cache.hpp"

#include <functional>
#include <sstream>
#include <string>

namespace dudu {
namespace {

std::string normalized_path_text(const std::filesystem::path& path) {
    return path.lexically_normal().string();
}

void append_values(std::ostringstream& out, const std::vector<std::string>& values) {
    out << values.size();
    for (const std::string& value : values) {
        out << '\0' << value;
    }
}

void append_dependencies(std::ostringstream& out,
                         const std::map<std::string, ProjectDependency>& dependencies) {
    out << dependencies.size();
    for (const auto& [name, dependency] : dependencies) {
        out << '\0' << name << '\0' << dependency.kind << '\0'
            << normalized_path_text(dependency.path) << '\0' << dependency.git << '\0'
            << dependency.rev << '\0' << dependency.tag << '\0' << dependency.branch << '\0'
            << normalized_path_text(dependency.resolved_root) << '\0' << dependency.resolved_rev;
    }
}

std::string fingerprint_config(const ProjectConfig& config) {
    std::ostringstream out;
    out << normalized_path_text(config.project_dir) << '\0'
        << normalized_path_text(config.manifest_path) << '\0' << config.name << '\0'
        << normalized_path_text(config.main) << '\0' << normalized_path_text(config.build_dir)
        << '\0' << config.cpp_std << '\0' << config.target_kind << '\0' << config.target_mode
        << '\0' << config.target_mode_explicit << '\0' << config.compiler << '\0';
    append_values(out, config.c_sources);
    append_values(out, config.cpp_sources);
    append_values(out, config.defines);
    append_values(out, config.flags);
    append_values(out, config.include_dirs);
    append_values(out, config.lib_dirs);
    append_values(out, config.libs);
    append_values(out, config.link_flags);
    append_values(out, config.pkg_config_packages);
    append_values(out, config.pkg_config_paths);
    append_dependencies(out, config.dependencies);
    out << normalized_path_text(config.cmake_source) << '\0' << config.cmake_target << '\0'
        << config.cmake_config << '\0' << config.cmake_generator;
    return out.str();
}

std::string fingerprint_build_values(const std::map<std::string, std::string>& values) {
    std::ostringstream out;
    for (const auto& [name, value] : values) {
        out << name << '\0' << value << '\0';
    }
    return out.str();
}

std::string
fingerprint_source_overrides(const std::map<std::filesystem::path, std::string>& source_overrides) {
    std::ostringstream out;
    for (const auto& [path, source] : source_overrides) {
        out << normalized_path_text(path) << '\0' << std::hash<std::string>{}(source) << '\0';
    }
    return out.str();
}

} // namespace

ProjectIndexCache::CacheKey ProjectIndexCache::key_for_options(const ProjectIndexOptions& options) {
    return {.entry_path = normalized_path_text(options.entry_path),
            .source_hash = std::hash<std::string>{}(std::string(options.entry_source)),
            .source_overrides_fingerprint = fingerprint_source_overrides(options.source_overrides),
            .source_dir = normalized_path_text(options.source_dir),
            .config_fingerprint = fingerprint_config(options.config),
            .build_values_fingerprint = fingerprint_build_values(options.build_values),
            .force_module_tree = options.force_module_tree,
            .allow_module_tree = options.allow_module_tree,
            .include_native_headers = options.include_native_headers,
            .include_native_headers_in_merged_module =
                options.include_native_headers_in_merged_module,
            .recover_syntax = options.recover_syntax,
            .check_semantics = options.check_semantics,
            .check_bodies = options.semantic_options.check_bodies};
}

const ProjectIndex& ProjectIndexCache::get(ProjectIndexOptions options) {
    const CacheKey key = key_for_options(options);
    if (const auto found = entries_.find(key); found != entries_.end()) {
        if (found->second.index.source_stamps_current()) {
            ++stats_.hits;
            return found->second.index;
        }
        ++stats_.stale_evictions;
        entries_.erase(found);
    }
    ++stats_.misses;
    CacheEntry entry{.index = ProjectIndex::load(options)};
    ++stats_.loads;
    auto result = entries_.emplace(key, std::move(entry));
    stats_.entries = entries_.size();
    return result.first->second.index;
}

ProjectIndexCacheStats ProjectIndexCache::stats() const {
    ProjectIndexCacheStats out = stats_;
    out.entries = entries_.size();
    return out;
}

void ProjectIndexCache::clear() {
    entries_.clear();
    stats_ = {};
}

} // namespace dudu
