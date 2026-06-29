#include "dudu/test_driver.hpp"

#include "dudu/cmake_backend.hpp"
#include "dudu/cmake_emit.hpp"
#include "dudu/decorators.hpp"
#include "dudu/file_io.hpp"
#include "dudu/native_build.hpp"
#include "dudu/project_config.hpp"
#include "dudu/project_driver.hpp"
#include "dudu/project_index.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
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
    return config_for_options(options);
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

bool unit_has_tests(const ModuleAst& module) {
    return std::any_of(module.functions.begin(), module.functions.end(), function_is_test);
}

bool module_has_tests(const ModuleAst& module) {
    if (unit_has_tests(module)) {
        return true;
    }
    return std::any_of(module.module_units.begin(), module.module_units.end(), unit_has_tests);
}

bool file_has_tests(const ProjectConfig& config, const std::filesystem::path& path) {
    const std::string source = read_required_text_file(path);
    ProjectIndexOptions index_options;
    index_options.entry_path = path;
    index_options.entry_source = source;
    index_options.config = config;
    index_options.source_dir = config.project_dir.empty() ? path.parent_path() : config.project_dir;
    index_options.allow_module_tree = false;
    index_options.include_native_headers = false;
    index_options.check_semantics = false;
    return module_has_tests(ProjectIndex::load(index_options).merged_module());
}

std::vector<std::filesystem::path> discover_test_files(const ProjectConfig& config,
                                                       const std::filesystem::path& root) {
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
        if (file_has_tests(config, it->path())) {
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
    const ProjectConfig config = build_config_for_options(options);
    const std::filesystem::path output =
        options.output.value_or(default_test_output(options, config));
    const bool project_output = options.project_driver && !options.quiet;
    const std::string target = output.filename().string();
    const std::filesystem::path root =
        output.parent_path() / (output.filename().string() + "-cmake");
    print_project_step(project_output, "cmake", root / "source" / "CMakeLists.txt");
    print_project_step(project_output, "build", root / "build");
    const std::filesystem::path bin = run_cmake_backend(
        {.config = config,
         .root = root,
         .cmake_lists = emit_cmake_test_project(config, options.input, target, options.test_filter,
                                                !options.no_capture),
         .target = target,
         .dudu_executable = options.dudu_executable,
         .stream_output = project_output,
         .timings = options.timings,
         .verbose = options.verbose});
    print_project_step(project_output, "test", bin);
    return std::system(shell_quote_path(bin).c_str()) == 0 ? 0 : 1;
}

int run_recursive_tests(TestDriverOptions options) {
    const std::filesystem::path root = recursive_test_input(options.input) ? "." : options.input;
    const ProjectConfig config = config_for_options(options);
    const std::vector<std::filesystem::path> files = discover_test_files(config, root);
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
