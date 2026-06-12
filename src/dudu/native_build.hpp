#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dudu {

struct NativeBuildOptions {
    std::filesystem::path output;
    ProjectConfig config;
    bool verbose = false;
};

std::string shell_quote_arg(const std::string& value);
std::string shell_quote_path(const std::filesystem::path& path);
std::string append_command_args(std::string command, const std::vector<std::string>& args);
std::filesystem::path build_executable(const NativeBuildOptions& options, const std::string& cpp);

} // namespace dudu
