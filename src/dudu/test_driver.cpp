#include "dudu/test_driver.hpp"

#include "dudu/cpp_emit.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/project_driver.hpp"
#include "dudu/sema.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        fail("could not open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::filesystem::path build_config_path(const std::filesystem::path& input) {
    const std::filesystem::path beside_input = input.parent_path() / "dudu.toml";
    if (!input.parent_path().empty() && std::filesystem::exists(beside_input)) {
        return beside_input;
    }
    return "dudu.toml";
}

ProjectConfig config_for_input(const std::filesystem::path& input) {
    return parse_project_config(build_config_path(input));
}

ProjectConfig config_for_options(const TestDriverOptions& options) {
    ProjectConfig config = config_for_input(options.input);
    if (!options.target_name.empty()) {
        config = apply_project_target(std::move(config), options.target_name);
    }
    return config;
}

ModuleAst checked_module(const TestDriverOptions& options, const std::string& source) {
    ModuleAst module = options.input.empty() ? parse_source(source, options.input)
                                             : load_source_tree(options.input);
    const ProjectConfig config = config_for_options(options);
    module.build_values = config.build_values;
    module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
    module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
    module.target_mode_explicit = config.target_mode_explicit;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
    const std::filesystem::path source_dir =
        options.input.empty() ? std::filesystem::current_path() : options.input.parent_path();
    merge_native_header_types(module, {.config = config, .source_dir = source_dir});
    analyze_module(module, {.check_bodies = true});
    return module;
}

bool looks_like_test_input(const std::filesystem::path& value) {
    return value.extension() == ".dd" || std::filesystem::exists(value);
}

bool recursive_test_input(const std::filesystem::path& value) {
    return value == "./..." || value == "...";
}

bool ignored_test_dir(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name == ".git" || name == "build";
}

std::vector<std::filesystem::path> discover_test_files(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::filesystem::recursive_directory_iterator it(root);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; ++it) {
        if (it->is_directory() && ignored_test_dir(it->path())) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file() || it->path().extension() != ".dd") {
            continue;
        }
        if (read_text_file(it->path()).find("@test") != std::string::npos) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

int run_delegated_project_tests() {
    std::string command = parse_project_config("dudu.toml").test_command;
    if (command.empty() && std::filesystem::exists("scripts/test.sh")) {
        command = "./scripts/test.sh";
    }
    if (command.empty()) {
        fail("missing test entry and no [test] command or scripts/test.sh");
    }
    return std::system(command.c_str()) == 0 ? 0 : 1;
}

int run_one_test_entry(TestDriverOptions options) {
    const std::string source = read_text_file(options.input);
    const ProjectConfig config = config_for_options(options);
    const std::filesystem::path output =
        options.output.value_or(config.build_dir.empty() ? std::filesystem::path("build/dudu_tests")
                                                         : config.build_dir / "dudu_tests");
    print_project_step(options.project_driver, "emit", output.string() + ".cpp");
    print_project_step(options.project_driver, "test", output);
    const std::filesystem::path bin = build_executable(
        {.output = output, .config = config, .verbose = options.verbose},
        emit_cpp_test_source(checked_module(options, source), options.test_filter));
    const std::filesystem::path command = bin.is_relative() && bin.parent_path().empty()
                                              ? std::filesystem::path(".") / bin
                                              : bin;
    return std::system(shell_quote_path(command).c_str()) == 0 ? 0 : 1;
}

int run_recursive_tests(TestDriverOptions options) {
    const std::filesystem::path root = recursive_test_input(options.input) ? "." : options.input;
    const std::vector<std::filesystem::path> files = discover_test_files(root);
    if (files.empty()) {
        std::cout << "running 0 tests\n"
                     "test result: ok. 0 passed; 0 failed; 0 filtered out\n";
        return 0;
    }
    int failed = 0;
    for (const std::filesystem::path& file : files) {
        TestDriverOptions child = options;
        child.input = file;
        child.target_name.clear();
        failed += run_one_test_entry(std::move(child)) == 0 ? 0 : 1;
    }
    return failed == 0 ? 0 : 1;
}

} // namespace

int run_project_tests(TestDriverOptions options) {
    if (recursive_test_input(options.input) ||
        (!options.input.empty() && std::filesystem::is_directory(options.input))) {
        return run_recursive_tests(std::move(options));
    }
    const ProjectConfig project = parse_project_config("dudu.toml");
    if (!options.input.empty() && !looks_like_test_input(options.input)) {
        const std::string input = options.input.string();
        if (project.targets.contains(input)) {
            options.target_name = input;
            options.input = apply_project_target(project, input).main;
            if (options.input.empty()) {
                fail("target has no entry: " + input);
            }
        } else {
            if (options.test_filter.empty()) {
                options.test_filter = input;
            }
            options.input.clear();
        }
    }
    if (options.input.empty()) {
        if (project.targets.contains("tests")) {
            options.target_name = "tests";
            options.input = apply_project_target(project, "tests").main;
        } else {
            options.input = project.main;
        }
    }
    if (options.input.empty()) {
        return run_delegated_project_tests();
    }
    return run_one_test_entry(std::move(options));
}

} // namespace dudu
