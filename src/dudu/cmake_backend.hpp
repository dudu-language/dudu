#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>
#include <optional>
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

struct UserCMakeBackendOptions {
    ProjectConfig config;
    std::filesystem::path root;
    bool verbose = false;
};

struct BuildCMakeProjectOptions {
    ProjectConfig config;
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;
    std::filesystem::path dudu_executable;
    bool verbose = false;
};

std::filesystem::path run_cmake_backend(const CMakeBackendOptions& options);
std::filesystem::path run_user_cmake_backend(const UserCMakeBackendOptions& options);
int run_user_cmake_tests(const UserCMakeBackendOptions& options);
std::filesystem::path build_cmake_project(const BuildCMakeProjectOptions& options);
std::filesystem::path default_cmake_backend_root(const ProjectConfig& config);
std::filesystem::path default_user_cmake_backend_root(const ProjectConfig& config);
std::filesystem::path cmake_backend_log_source(const ProjectConfig& config);
std::filesystem::path cmake_backend_log_build_dir(const ProjectConfig& config);
std::string cmake_target_name(const ProjectConfig& config, const std::filesystem::path& input);
std::string user_cmake_target_name(const ProjectConfig& config);
bool uses_user_cmake_backend(const ProjectConfig& config);

} // namespace dudu
