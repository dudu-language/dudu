#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>
#include <string>

namespace dudu {

std::string emit_cmake_project(const ProjectConfig& config, const std::filesystem::path& input);

} // namespace dudu
