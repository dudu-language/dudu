#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>
#include <string>

namespace dudu {

std::string emit_cmake_project(const ProjectConfig& config, const std::filesystem::path& input);
std::string emit_cmake_cpp_project(const ProjectConfig& config, const std::string& target,
                                   const std::filesystem::path& cpp_source);

} // namespace dudu
