#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct ProjectTarget {
    std::filesystem::path main;
    std::string target_kind;
    std::string target_mode;
    bool target_mode_explicit = false;
    std::vector<std::string> c_sources;
    std::vector<std::string> cpp_sources;
    std::vector<std::string> defines;
    std::vector<std::string> flags;
    std::vector<std::string> include_dirs;
    std::vector<std::string> lib_dirs;
    std::vector<std::string> libs;
    std::vector<std::string> link_flags;
    std::vector<std::string> pkg_config_packages;
    std::vector<std::string> pkg_config_paths;
};

struct ProjectConfig {
    std::filesystem::path project_dir;
    std::filesystem::path manifest_path;
    std::string name;
    std::filesystem::path main;
    std::filesystem::path build_dir;
    std::string cpp_std = "c++20";
    std::string target_kind = "executable";
    std::string target_mode = "hosted";
    bool target_mode_explicit = false;
    std::string bench_command;
    std::string test_command;
    std::map<std::string, ProjectTarget> targets;
    std::map<std::string, std::string> build_values;
    std::string compiler;
    std::vector<std::string> c_sources;
    std::vector<std::string> cpp_sources;
    std::vector<std::string> defines;
    std::vector<std::string> flags;
    std::vector<std::string> include_dirs;
    std::vector<std::string> lib_dirs;
    std::vector<std::string> libs;
    std::vector<std::string> link_flags;
    std::vector<std::string> pkg_config_packages;
    std::vector<std::string> pkg_config_paths;
    std::filesystem::path cmake_source;
    std::string cmake_target;
    std::string cmake_config;
    std::string cmake_generator;
};

ProjectConfig apply_project_target(ProjectConfig config, const std::string& target_name);
std::filesystem::path find_project_config(const std::filesystem::path& input);
ProjectConfig parse_project_config(const std::filesystem::path& path);
std::filesystem::path project_path(const ProjectConfig& config, const std::filesystem::path& path);

} // namespace dudu
