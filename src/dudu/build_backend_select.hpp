#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>

namespace dudu {

ProjectConfig select_build_backend(ProjectConfig config, const std::filesystem::path& input,
                                   bool project_driver = false);

} // namespace dudu
