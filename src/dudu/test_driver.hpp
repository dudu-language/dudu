#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace dudu {

struct TestDriverOptions {
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;
    std::map<std::string, std::string> build_values;
    std::filesystem::path dudu_executable = "dudu";
    std::string target_name;
    std::string test_filter;
    bool no_capture = false;
    bool project_driver = false;
    bool quiet = false;
    bool verbose = false;
};

int run_project_tests(TestDriverOptions options);

} // namespace dudu
