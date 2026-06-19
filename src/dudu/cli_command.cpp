#include "dudu/cli_command.hpp"

#include "dudu/cli_options.hpp"
#include "dudu/cmake_backend.hpp"
#include "dudu/cmake_emit.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_emit_modules.hpp"
#include "dudu/format_path.hpp"
#include "dudu/language_server.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_header_cache.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/project_driver.hpp"
#include "dudu/sema.hpp"
#include "dudu/test_driver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

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

std::optional<std::filesystem::path> find_executable_on_path(const std::filesystem::path& name) {
    const char* env_path = std::getenv("PATH");
    if (env_path == nullptr || name.empty() || name.has_parent_path()) {
        return std::nullopt;
    }
    const std::string path = env_path;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(':', start);
        const std::filesystem::path dir =
            path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::filesystem::path candidate = dir / name;
        std::error_code error;
        if (std::filesystem::exists(candidate, error) && !error) {
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(candidate, error);
            return error ? candidate : canonical;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return std::nullopt;
}

std::filesystem::path executable_path(char* executable) {
    const std::filesystem::path raw = executable == nullptr ? "dudu" : executable;
    if (const std::optional<std::filesystem::path> found = find_executable_on_path(raw)) {
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

void write_text_output(const std::optional<std::filesystem::path>& path, const std::string& text) {
    if (!path.has_value() || path->empty()) {
        std::cout << text;
        return;
    }
    std::filesystem::create_directories(path->parent_path().empty() ? "." : path->parent_path());
    std::ofstream out(*path);
    if (!out) {
        fail("could not open output " + path->string());
    }
    out << text;
}

int run_project_benchmarks(const CliOptions& options) {
    const ProjectConfig config = parse_project_config(build_config_path(options.input));
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

ProjectConfig config_for_input(const std::filesystem::path& input) {
    return parse_project_config(build_config_path(input));
}

std::filesystem::path source_dir_for_input(const std::filesystem::path& input) {
    if (input.empty()) {
        return std::filesystem::current_path();
    }
    return input.has_parent_path() ? input.parent_path() : std::filesystem::current_path();
}

NativeHeaderOptions native_header_options_for_clean_cache(const CliOptions& options) {
    if (options.input.empty() || std::filesystem::is_directory(options.input)) {
        const std::filesystem::path root = options.input.empty() ? "." : options.input;
        return {.config = parse_project_config(root / "dudu.toml"), .source_dir = root};
    }
    return {.config = config_for_input(options.input),
            .source_dir = source_dir_for_input(options.input)};
}

ProjectConfig config_for_options(const CliOptions& options) {
    ProjectConfig config = config_for_input(options.input);
    if (!options.target_name.empty()) {
        config = apply_project_target(std::move(config), options.target_name);
    }
    return config;
}

ProjectConfig build_config_for_options(const CliOptions& options) {
    ProjectConfig config = config_for_options(options);
    if (!config.build_backend_explicit && config.build_backend == "direct" &&
        source_tree_files(options.input).size() > 1) {
        config.build_backend = "cmake";
    }
    return config;
}

FormatPathOptions format_options_for_project(const CliOptions& options) {
    FormatPathOptions out;
    if (!options.project_driver || !std::filesystem::is_directory(options.input)) {
        return out;
    }
    const std::filesystem::path config_path = build_config_path(options.input);
    if (config_path.empty() || !std::filesystem::exists(config_path)) {
        return out;
    }
    const ProjectConfig config = parse_project_config(config_path);
    out.excluded_dirs.push_back(project_path(config, ".git"));
    out.excluded_dirs.push_back(project_path(config, "build"));
    if (!config.build_dir.empty()) {
        out.excluded_dirs.push_back(project_path(config, config.build_dir));
    }
    return out;
}

std::filesystem::path default_build_output(const ProjectConfig& config,
                                           const std::filesystem::path& input) {
    if (config.name.empty() && config.build_dir.empty()) {
        return "a.out";
    }
    const std::string name = config.name.empty() ? input.stem().string() : config.name;
    std::filesystem::path filename = name;
    if (config.target_kind == "library") {
        filename = "lib" + name + ".a";
    } else if (config.target_kind == "shared_library") {
        filename = "lib" + name + ".so";
    }
    return config.build_dir.empty() ? filename : project_path(config, config.build_dir) / filename;
}

ModuleAst checked_module(const CliOptions& options, const std::string& source, bool check_bodies) {
    ModuleAst module = options.input.empty() ? parse_source(source, options.input)
                                             : load_source_tree(options.input);
    const ProjectConfig config =
        options.input.empty() ? config_for_options(options) : build_config_for_options(options);
    module.build_values = config.build_values;
    module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
    module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
    module.target_mode_explicit = config.target_mode_explicit;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
    const std::filesystem::path source_dir = source_dir_for_input(options.input);
    const NativeHeaderOptions native_header_options{.config = config, .source_dir = source_dir};
    merge_native_header_types(module, native_header_options);
    for (ModuleAst& unit : module.module_units) {
        unit.build_values = module.build_values;
        unit.target_mode_explicit = module.target_mode_explicit;
        merge_native_header_types(unit, native_header_options);
    }
    if (options.emit_modules || config.build_backend == "cmake") {
        analyze_module_tree(module, {.check_bodies = check_bodies});
    } else {
        reject_direct_backend_module_conflicts(module);
        analyze_module(module, {.check_bodies = check_bodies});
    }
    return module;
}

void check_source_file(CliOptions options, const std::filesystem::path& path) {
    options.input = path;
    (void)checked_module(options, read_text_file(path), true);
}

bool check_source_path(const CliOptions& options) {
    if (!std::filesystem::is_directory(options.input)) {
        check_source_file(options, options.input);
        return true;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(options.input)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dd") {
            continue;
        }
        check_source_file(options, entry.path());
    }
    return true;
}

int run_build_command(const CliOptions& options, char* executable) {
    const ProjectConfig config = build_config_for_options(options);
    const std::filesystem::path dudu_executable = executable_path(executable);
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "backend", config.build_backend);
    print_project_step(project_output, "entry", options.input);
    if (config.build_backend == "cmake") {
        print_project_step(project_output, "cmake", cmake_backend_log_source(config));
        print_project_step(project_output, "build", cmake_backend_log_build_dir(config));
        (void)build_cmake_project({.config = config,
                                   .input = options.input,
                                   .output = options.output,
                                   .dudu_executable = dudu_executable,
                                   .stream_output = project_output,
                                   .verbose = options.verbose});
        return 0;
    }
    const std::string source = read_text_file(options.input);
    const std::filesystem::path output =
        options.output.value_or(default_build_output(config, options.input));
    print_project_step(project_output, "emit", output.string() + ".cpp");
    print_project_step(project_output, "build", output);
    (void)build_executable({.output = output,
                            .config = config,
                            .stream_output = project_output,
                            .verbose = options.verbose},
                           emit_cpp_source(checked_module(options, source, true)));
    return 0;
}

int run_run_command(const CliOptions& options, char* executable) {
    const ProjectConfig config = build_config_for_options(options);
    const std::filesystem::path dudu_executable = executable_path(executable);
    if (config.target_kind != "executable") {
        fail("cannot run target kind: " + config.target_kind);
    }
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "backend", config.build_backend);
    print_project_step(project_output, "entry", options.input);
    if (config.build_backend == "cmake") {
        print_project_step(project_output, "cmake", cmake_backend_log_source(config));
        print_project_step(project_output, "build", cmake_backend_log_build_dir(config));
        const std::filesystem::path bin = build_cmake_project({.config = config,
                                                               .input = options.input,
                                                               .output = options.output,
                                                               .dudu_executable = dudu_executable,
                                                               .stream_output = project_output,
                                                               .verbose = options.verbose});
        print_project_step(project_output, "run", bin);
        return std::system(shell_quote_path(bin).c_str()) == 0 ? 0 : 1;
    }
    const std::string source = read_text_file(options.input);
    const std::filesystem::path output =
        options.output.value_or(default_build_output(config, options.input));
    print_project_step(project_output, "emit", output.string() + ".cpp");
    print_project_step(project_output, "build", output);
    const std::filesystem::path bin =
        build_executable({.output = output,
                          .config = config,
                          .stream_output = project_output,
                          .verbose = options.verbose},
                         emit_cpp_source(checked_module(options, source, true)));
    const std::filesystem::path command =
        bin.is_relative() && bin.parent_path().empty() ? std::filesystem::path(".") / bin : bin;
    print_project_step(project_output, "run", command);
    return std::system(shell_quote_path(command).c_str()) == 0 ? 0 : 1;
}

} // namespace

int run_cli(int argc, char** argv) {
    const std::string executable =
        argc > 0 ? std::filesystem::path(argv[0]).stem().string() : "duc";
    const bool project_driver = executable == "dudu";
    const CliOptions options = resolve_project_input(parse_cli_options(argc, argv, project_driver));
    if (options.lsp) {
        return run_language_server(std::cin, std::cout, std::cerr);
    }
    if (options.init_project) {
        init_project(options.input.empty() ? std::filesystem::path(".") : options.input);
        return 0;
    }
    if (options.new_project) {
        if (options.input.empty()) {
            fail("dudu new requires a project name");
        }
        new_project(options.input);
        return 0;
    }
    if (options.bench) {
        return run_project_benchmarks(options);
    }
    if (options.clean) {
        const std::filesystem::path cleaned =
            clean_project(options.input.empty() ? std::filesystem::path(".") : options.input);
        if (!options.quiet) {
            std::cerr << "clean " << cleaned.string() << '\n';
        }
        return 0;
    }
    if (options.clean_cache) {
        const std::filesystem::path cleaned =
            clean_native_header_cache(native_header_options_for_clean_cache(options));
        if (!options.quiet) {
            std::cerr << "clean-cache " << cleaned.string() << '\n';
        }
        return 0;
    }
    if (options.test) {
        return run_project_tests({.input = options.input,
                                  .output = options.output,
                                  .build_values = options.build_values,
                                  .dudu_executable = argv[0],
                                  .target_name = options.target_name,
                                  .test_filter = options.test_filter,
                                  .no_capture = options.no_capture,
                                  .project_driver = options.project_driver,
                                  .quiet = options.quiet,
                                  .verbose = options.verbose});
    }
    if (options.format) {
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
    if (options.check) {
        print_project_step(options.project_driver && !options.quiet, "check", options.input);
        (void)check_source_path(options);
        print_project_step(options.project_driver && !options.quiet, "ok", options.input);
        return 0;
    }
    if (options.cmake) {
        const ProjectConfig config = config_for_options(options);
        print_project_step(options.project_driver && !options.quiet, "cmake",
                           options.output.value_or("CMakeLists.txt"));
        write_text_output(options.output, emit_cmake_project(config, options.input));
        return 0;
    }
    if (options.build) {
        return run_build_command(options, argv[0]);
    }
    if (options.run) {
        return run_run_command(options, argv[0]);
    }

    const std::string source = read_text_file(options.input);
    if (options.header_output.has_value()) {
        write_text_output(options.header_output,
                          emit_cpp_header(checked_module(options, source, false)));
        return 0;
    }
    if (options.c_header_output.has_value()) {
        write_text_output(options.c_header_output,
                          emit_c_header(checked_module(options, source, false)));
        return 0;
    }
    if (options.emit_modules) {
        if (!options.output.has_value()) {
            fail("emit-modules requires -o <directory>");
        }
        const bool project_output = options.project_driver && !options.quiet;
        print_project_step(project_output, "analyze", options.input);
        const ModuleAst module = checked_module(options, source, true);
        print_project_step(project_output, "emit", *options.output);
        write_cpp_module_artifacts(*options.output, module);
        return 0;
    }
    if (options.emit_test_modules) {
        if (!options.output.has_value()) {
            fail("emit-test-modules requires -o <directory>");
        }
        const bool project_output = options.project_driver && !options.quiet;
        print_project_step(project_output, "analyze", options.input);
        const ModuleAst module = checked_module(options, source, true);
        print_project_step(project_output, "emit", *options.output);
        for (const CppModuleArtifact& artifact :
             emit_cpp_test_module_artifacts(module, options.test_filter, !options.no_capture)) {
            write_text_output(*options.output / artifact.path, artifact.content);
        }
        return 0;
    }
    if (options.emit_cpp) {
        write_text_output(options.output, emit_cpp_source(checked_module(options, source, true)));
        return 0;
    }
    write_text_output(std::nullopt, emit_cpp_source(checked_module(options, source, true)));
    return 0;
}

} // namespace dudu
