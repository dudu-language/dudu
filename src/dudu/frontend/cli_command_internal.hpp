#pragma once

#include "dudu/frontend/cli_options.hpp"
#include "dudu/project/project_config.hpp"

#include <filesystem>

namespace dudu {

std::filesystem::path cli_executable_path(char* executable);
ProjectConfig cli_project_config(const CliOptions& options);

int run_project_benchmarks(const CliOptions& options);
int run_clean_native_cache_command(const CliOptions& options);
int run_deps_fetch_command(const CliOptions& options);
int run_format_command(const CliOptions& options);
int run_cmake_command(const CliOptions& options);
int run_build_command(const CliOptions& options, char* executable);
int run_run_command(const CliOptions& options, char* executable);
int run_compile_command(const CliOptions& options, char* executable);

} // namespace dudu
