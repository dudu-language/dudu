#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct ProjectConfig {
    std::filesystem::path main;
    std::string cpp_std = "c++20";
    std::string bench_command;
    std::string test_command;
    std::map<std::string, std::string> build_values;
    std::vector<std::string> include_dirs;
    std::vector<std::string> libs;
};

ProjectConfig parse_project_config(const std::filesystem::path& path);

} // namespace dudu
