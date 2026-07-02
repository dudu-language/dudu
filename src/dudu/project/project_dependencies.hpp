#pragma once

#include "dudu/project/project_config.hpp"

#include <filesystem>
#include <map>

namespace dudu {

std::filesystem::path dependency_cache_root(const ProjectConfig& config);
std::filesystem::path dependency_package_root(const ProjectConfig& config,
                                              const ProjectDependency& dependency);
std::filesystem::path dependency_source_root(const std::filesystem::path& package_root);
std::map<std::string, std::filesystem::path> dependency_module_roots(const ProjectConfig& config);
void ensure_project_dependencies(ProjectConfig& config, bool update, bool quiet);

} // namespace dudu
