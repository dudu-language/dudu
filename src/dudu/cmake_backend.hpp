#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>
#include <string>

namespace dudu {

struct CMakeBackendOptions {
    ProjectConfig config;
    std::filesystem::path root;
    std::string cmake_lists;
    std::string target;
    std::filesystem::path dudu_executable;
    bool verbose = false;
};

std::filesystem::path run_cmake_backend(const CMakeBackendOptions& options);
std::filesystem::path default_cmake_backend_root(const ProjectConfig& config);
std::string cmake_target_name(const ProjectConfig& config, const std::filesystem::path& input);

} // namespace dudu
