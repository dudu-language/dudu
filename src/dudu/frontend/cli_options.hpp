#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct CliOptions {
    std::filesystem::path input;
    std::optional<std::filesystem::path> c_header_output;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> header_output;
    std::map<std::string, std::string> build_values;
    std::string target_name;
    std::vector<std::string> command_args;
    std::string test_filter;
    bool bench = false;
    bool build = false;
    bool check = false;
    bool clean = false;
    bool clean_cache = false;
    bool cmake = false;
    bool deps_fetch = false;
    bool emit_cpp = false;
    bool emit_modules = false;
    bool emit_test_modules = false;
    bool expand_macros = false;
    bool format = false;
    bool init_project = false;
    bool new_project = false;
    bool no_capture = false;
    bool project_driver = false;
    bool quiet = false;
    bool run = false;
    bool test = false;
    bool timings = false;
    bool toolchain_update = false;
    bool uninstall = false;
    bool verbose = false;
};

CliOptions parse_cli_options(int argc, char** argv, bool project_driver);
CliOptions resolve_project_input(CliOptions options);
std::filesystem::path build_config_path(const std::filesystem::path& input);

} // namespace dudu
