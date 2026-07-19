#include "dudu/format/format_path.hpp"
#include "dudu/frontend/cli_command_internal.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/native/native_header_cache.hpp"
#include "dudu/project/cmake_backend.hpp"
#include "dudu/project/cmake_emit.hpp"
#include "dudu/project/project_dependencies.hpp"
#include "dudu/project/project_driver.hpp"
#include "dudu/support/executable.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

FormatPathOptions format_options_for_project(const CliOptions& options) {
    FormatPathOptions format_options;
    if (!options.project_driver || !std::filesystem::is_directory(options.input)) {
        return format_options;
    }
    const std::filesystem::path config_path = find_project_config(options.input);
    if (config_path.empty() || !std::filesystem::exists(config_path)) {
        return format_options;
    }
    const ProjectConfig config = parse_project_config(config_path);
    format_options.excluded_dirs.push_back(project_path(config, ".git"));
    format_options.excluded_dirs.push_back(project_path(config, "build"));
    if (!config.build_dir.empty()) {
        format_options.excluded_dirs.push_back(project_path(config, config.build_dir));
    }
    return format_options;
}

} // namespace

std::filesystem::path cli_executable_path(char* executable) {
    const std::filesystem::path raw = executable == nullptr ? "dudu" : executable;
    if (const std::optional<std::filesystem::path> found = find_executable(raw)) {
        return *found;
    }
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(raw, error);
    if (error) {
        return raw;
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute : canonical;
}

std::filesystem::path cli_compiler_path(char* executable) {
    const std::filesystem::path tool = cli_executable_path(executable);
    if (tool.stem() == "duc") {
        return tool;
    }

    const std::filesystem::path compiler_name =
        std::filesystem::path("duc").replace_extension(tool.extension());
    const std::filesystem::path sibling = tool.parent_path() / compiler_name;
    std::error_code error;
    if (std::filesystem::is_regular_file(sibling, error) && !error) {
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(sibling, error);
        return error ? sibling : canonical;
    }
    if (const std::optional<std::filesystem::path> found = find_executable(compiler_name)) {
        return *found;
    }
    fail("could not find the duc compiler beside " + tool.string() + " or on PATH");
}

std::filesystem::path cli_source_dir_for_input(const std::filesystem::path& input) {
    if (input.empty()) {
        return std::filesystem::current_path();
    }
    return input.has_parent_path() ? input.parent_path() : std::filesystem::current_path();
}

ProjectConfig cli_project_config(const CliOptions& options) {
    ProjectConfig config = parse_project_config(find_project_config(options.input));
    if (!options.target_name.empty()) {
        config = apply_project_target(std::move(config), options.target_name);
    }
    if (options.project_driver) {
        ensure_project_dependencies(config, false, options.quiet);
    }
    if (!config.dependencies.empty()) {
        merge_dependency_native_inputs(config);
    }
    return config;
}

int run_project_benchmarks(const CliOptions& options) {
    const ProjectConfig config = parse_project_config(find_project_config(options.input));
    if (!options.command_args.empty() && options.command_args.front() == "compiler") {
        const std::filesystem::path script = project_path(config, "scripts/bench_compiler.sh");
        if (!std::filesystem::exists(script)) {
            fail("missing scripts/bench_compiler.sh for compiler benchmark");
        }
        std::vector<std::string> args(options.command_args.begin() + 1, options.command_args.end());
        const std::string command = append_command_args(shell_quote_path(script), args);
        print_project_step(options.project_driver && !options.quiet, "bench", command);
        return std::system(project_shell_command(config, command).c_str()) == 0 ? 0 : 1;
    }

    std::string command = config.bench_command;
    if (command.empty() && std::filesystem::exists(project_path(config, "scripts/bench.sh"))) {
        command = "./scripts/bench.sh";
    }
    if (command.empty()) {
        fail("missing [bench] command and scripts/bench.sh");
    }
    const std::string effective_command = append_command_args(command, options.command_args);
    print_project_step(options.project_driver && !options.quiet, "bench", effective_command);
    return std::system(project_shell_command(config, effective_command).c_str()) == 0 ? 0 : 1;
}

int run_clean_native_cache_command(const CliOptions& options) {
    NativeHeaderOptions native_options;
    if (options.input.empty() || std::filesystem::is_directory(options.input)) {
        const std::filesystem::path root = options.input.empty() ? "." : options.input;
        native_options = {.config = parse_project_config(root / "dudu.toml"), .source_dir = root};
    } else {
        native_options = {.config = parse_project_config(find_project_config(options.input)),
                          .source_dir = cli_source_dir_for_input(options.input)};
    }
    const std::filesystem::path cleaned = clean_native_header_cache(native_options);
    if (!options.quiet) {
        std::cerr << "clean-cache " << cleaned.string() << '\n';
    }
    return 0;
}

int run_deps_fetch_command(const CliOptions& options) {
    const std::filesystem::path root =
        options.input.empty() ? std::filesystem::path(".") : options.input;
    ProjectConfig config = parse_project_config(find_project_config(root));
    ensure_project_dependencies(config, true, options.quiet);
    print_project_step(options.project_driver && !options.quiet, "deps", "fetched");
    return 0;
}

int run_format_command(const CliOptions& options) {
    const FormatPathOptions format_options = format_options_for_project(options);
    if (options.check) {
        return check_formatted_path(options.input, format_options) ? 0 : 1;
    }
    if (options.project_driver && !options.output.has_value()) {
        format_path_in_place(options.input, format_options);
        return 0;
    }
    format_path(options.input, options.output, std::cout, format_options);
    return 0;
}

int run_cmake_command(const CliOptions& options) {
    const ProjectConfig config = cli_project_config(options);
    print_project_step(options.project_driver && !options.quiet, "cmake",
                       options.output.value_or("CMakeLists.txt"));
    const std::string generated = emit_cmake_project(config, options.input);
    if (!options.output.has_value() || options.output->empty()) {
        std::cout << generated;
        return 0;
    }
    std::filesystem::create_directories(options.output->parent_path().empty()
                                            ? std::filesystem::path(".")
                                            : options.output->parent_path());
    std::ofstream out(*options.output);
    if (!out) {
        fail("could not open output " + options.output->string());
    }
    out << generated;
    return 0;
}

int run_build_command(const CliOptions& options, char* executable) {
    const ProjectConfig config = cli_project_config(options);
    const std::filesystem::path compiler_executable = cli_compiler_path(executable);
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "backend", "cmake");
    print_project_step(project_output, "entry", options.input);
    print_project_step(project_output, "cmake", cmake_backend_log_source(config));
    print_project_step(project_output, "build", cmake_backend_log_build_dir(config));
    const std::filesystem::path bin =
        build_cmake_project({.config = config,
                             .input = options.input,
                             .output = options.output,
                             .compiler_executable = compiler_executable,
                             .stream_output = project_output,
                             .timings = options.timings,
                             .verbose = options.verbose});
    print_project_step(project_output, "output", bin);
    return 0;
}

int run_run_command(const CliOptions& options, char* executable) {
    const ProjectConfig config = cli_project_config(options);
    if (config.target_kind != "executable") {
        fail("cannot run target kind: " + config.target_kind);
    }
    const std::filesystem::path compiler_executable = cli_compiler_path(executable);
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "backend", "cmake");
    print_project_step(project_output, "entry", options.input);
    print_project_step(project_output, "cmake", cmake_backend_log_source(config));
    print_project_step(project_output, "build", cmake_backend_log_build_dir(config));
    const std::filesystem::path bin =
        build_cmake_project({.config = config,
                             .input = options.input,
                             .output = options.output,
                             .compiler_executable = compiler_executable,
                             .stream_output = project_output,
                             .timings = options.timings,
                             .verbose = options.verbose});
    const std::string command = append_command_args(shell_quote_path(bin), options.command_args);
    print_project_step(project_output, "run", command);
    return std::system(command.c_str()) == 0 ? 0 : 1;
}

} // namespace dudu
