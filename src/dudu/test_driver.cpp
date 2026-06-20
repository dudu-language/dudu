#include "dudu/test_driver.hpp"

#include "dudu/build_backend_select.hpp"
#include "dudu/cmake_backend.hpp"
#include "dudu/cmake_emit.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/decorators.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/project_driver.hpp"
#include "dudu/sema.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
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
    return find_project_config(input);
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

ProjectConfig build_config_for_options(const TestDriverOptions& options) {
    ProjectConfig config = config_for_options(options);
    return select_build_backend(std::move(config), options.input, options.project_driver);
}

std::filesystem::path source_dir_for_input(const std::filesystem::path& input) {
    if (input.empty()) {
        return std::filesystem::current_path();
    }
    return input.has_parent_path() ? input.parent_path() : std::filesystem::current_path();
}

ModuleAst checked_module(const TestDriverOptions& options, const std::string& source) {
    ModuleAst module = options.input.empty() ? parse_source(source, options.input)
                                             : load_source_tree(options.input);
    const ProjectConfig config = build_config_for_options(options);
    module.build_values = config.build_values;
    module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
    module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
    module.target_mode_explicit = config.target_mode_explicit;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
    const std::filesystem::path source_dir = source_dir_for_input(options.input);
    merge_native_header_types(module, {.config = config, .source_dir = source_dir});
    if (config.build_backend == "cmake") {
        analyze_module_tree(module, {.check_bodies = true});
    } else {
        reject_merged_output_module_conflicts(module);
        analyze_module(module, {.check_bodies = true});
    }
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

bool function_is_test(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (decorator_matches(decorator, "test") || decorator_matches(decorator, "test.ignore") ||
            decorator_matches(decorator, "test.should_panic") ||
            decorator_call_matches(decorator, "test.should_panic")) {
            return true;
        }
    }
    return false;
}

bool module_has_tests(const ModuleAst& module) {
    return std::any_of(module.functions.begin(), module.functions.end(), function_is_test);
}

bool file_has_tests(const std::filesystem::path& path) {
    return module_has_tests(parse_source(read_text_file(path), path));
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
        if (file_has_tests(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool has_project_test_script(const ProjectConfig& config) {
    return std::filesystem::exists(project_path(config, "scripts/test.sh"));
}

int run_delegated_project_tests(const ProjectConfig& config) {
    std::string command = config.test_command;
    if (command.empty() && has_project_test_script(config)) {
        command = "./scripts/test.sh";
    }
    if (command.empty()) {
        fail("missing test entry and no [test] command or scripts/test.sh");
    }
    return std::system(project_shell_command(config, command).c_str()) == 0 ? 0 : 1;
}

int run_user_owned_cmake_project_tests(const ProjectConfig& config,
                                       const TestDriverOptions& options) {
    const std::filesystem::path root = default_user_cmake_backend_root(config);
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "cmake", cmake_backend_log_source(config));
    print_project_step(project_output, "build", cmake_backend_log_build_dir(config));
    print_project_step(project_output, "test", cmake_backend_log_build_dir(config));
    return run_user_cmake_tests({.config = config,
                                 .root = root,
                                 .stream_output = project_output,
                                 .verbose = options.verbose});
}

uint64_t fnv1a(std::string_view text) {
    uint64_t hash = 14695981039346656037ull;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex_hash(uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

std::string safe_stem(std::filesystem::path path) {
    std::string stem = path.stem().string();
    if (stem.empty()) {
        stem = "tests";
    }
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            c = '_';
        }
    }
    return stem;
}

std::filesystem::path default_test_output(const TestDriverOptions& options,
                                          const ProjectConfig& config) {
    const std::filesystem::path root = config.build_dir.empty()
                                           ? std::filesystem::path("build/dudu-tests")
                                           : project_path(config, config.build_dir) / "dudu-tests";
    const std::string key = std::filesystem::absolute(options.input).string() + "|" +
                            options.target_name + "|" + options.test_filter + "|" +
                            config.target_kind + "|" + config.target_mode + "|" +
                            (options.no_capture ? "nocapture" : "capture");
    return root / (safe_stem(options.input) + "-" + hex_hash(fnv1a(key)));
}

int run_one_test_entry(TestDriverOptions options) {
    const std::string source = read_text_file(options.input);
    const ProjectConfig config = build_config_for_options(options);
    const std::filesystem::path output =
        options.output.value_or(default_test_output(options, config));
    if (config.build_backend == "cmake") {
        const bool project_output = options.project_driver && !options.quiet;
        const std::string target = output.filename().string();
        const std::filesystem::path root =
            output.parent_path() / (output.filename().string() + "-cmake");
        print_project_step(project_output, "cmake", root / "source" / "CMakeLists.txt");
        print_project_step(project_output, "build", root / "build");
        const std::filesystem::path bin = run_cmake_backend(
            {.config = config,
             .root = root,
             .cmake_lists = emit_cmake_test_project(config, options.input, target,
                                                    options.test_filter, !options.no_capture),
             .target = target,
             .dudu_executable = options.dudu_executable,
             .stream_output = project_output,
             .timings = options.timings,
             .verbose = options.verbose});
        print_project_step(project_output, "test", bin);
        return std::system(shell_quote_path(bin).c_str()) == 0 ? 0 : 1;
    }
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "emit", output.string() + ".cpp");
    print_project_step(project_output, "test", output);
    const std::string harness = emit_cpp_test_source(checked_module(options, source),
                                                     options.test_filter, !options.no_capture);
    const std::filesystem::path bin = build_executable({.output = output,
                                                        .config = config,
                                                        .stream_output = project_output,
                                                        .verbose = options.verbose},
                                                       harness);
    const std::filesystem::path command =
        bin.is_relative() && bin.parent_path().empty() ? std::filesystem::path(".") / bin : bin;
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
    const ProjectConfig project = parse_project_config(build_config_path(options.input));
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
        if (uses_user_cmake_backend(project) && project.test_command.empty() &&
            !has_project_test_script(project)) {
            return run_user_owned_cmake_project_tests(project, options);
        }
        return run_delegated_project_tests(project);
    }
    return run_one_test_entry(std::move(options));
}

} // namespace dudu
