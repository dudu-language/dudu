#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>
#include <string>

namespace dudu {

std::string emit_cmake_project(const ProjectConfig& config, const std::filesystem::path& input);
std::string emit_cmake_project(const ProjectConfig& config, const std::filesystem::path& input,
                               const std::string& target);
std::string emit_cmake_test_project(const ProjectConfig& config, const std::filesystem::path& input,
                                    const std::string& target, const std::string& filter = {},
                                    bool capture_output = true);

} // namespace dudu
