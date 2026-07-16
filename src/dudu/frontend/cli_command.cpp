#include "dudu/frontend/cli_command.hpp"

#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/format/format_path.hpp"
#include "dudu/frontend/cli_options.hpp"
#include "dudu/macro/macro_expansion_render.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/native/native_header_cache.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/project/cmake_backend.hpp"
#include "dudu/project/cmake_emit.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_dependencies.hpp"
#include "dudu/project/project_driver.hpp"
#include "dudu/project/project_index_cache.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/support/toolchain_manager.hpp"
#include "dudu/testing/test_driver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dudu {
namespace {

ProjectIndexCache cli_project_index_cache;

using Clock = std::chrono::steady_clock;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string milliseconds(std::uint64_t nanoseconds) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << static_cast<double>(nanoseconds) / 1'000'000.0 << " ms";
    return out.str();
}

std::uint64_t elapsed_ns(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

void print_macro_performance(bool enabled, const macro::ExpansionReport& report) {
    if (!enabled || (report.invocations == 0 && report.definitions.empty()))
        return;
    print_project_step(true, "macro.requests", std::to_string(report.invocations));
    print_project_step(true, "macro.executions", std::to_string(report.worker_executions));
    print_project_step(true, "macro.worker_starts", std::to_string(report.worker_starts));
    print_project_step(true, "macro.expansion_cache_hits",
                       std::to_string(report.expansion_cache_hits));
    print_project_step(true, "macro.generated_nodes", std::to_string(report.generated_nodes));
    print_project_step(true, "macro.worker_rss", std::to_string(report.worker_rss_kb) + " KiB");
    print_project_step(true, "macro.plan", milliseconds(report.timings.plan_ns));
    print_project_step(true, "macro.setup", milliseconds(report.timings.setup_ns));
    print_project_step(true, "macro.declaration_bridge",
                       milliseconds(report.timings.declaration_bridge_ns));
    print_project_step(true, "macro.request_loop", milliseconds(report.timings.request_loop_ns));
    print_project_step(true, "macro.package_build", milliseconds(report.timings.package_build_ns));
    print_project_step(true, "macro.package_sdk_prepare",
                       milliseconds(report.timings.package_sdk_prepare_ns));
    print_project_step(true, "macro.package_compile",
                       milliseconds(report.timings.package_compile_ns));
    print_project_step(true, "macro.package_link", milliseconds(report.timings.package_link_ns));
    print_project_step(true, "macro.worker_start", milliseconds(report.timings.worker_start_ns));
    print_project_step(true, "macro.protocol", milliseconds(report.timings.protocol_ns));
    print_project_step(true, "macro.execute", milliseconds(report.timings.execute_ns));
    print_project_step(true, "macro.cache_read", milliseconds(report.timings.cache_read_ns));
    print_project_step(true, "macro.cache_key", milliseconds(report.timings.cache_key_ns));
    print_project_step(true, "macro.cache_write", milliseconds(report.timings.cache_write_ns));
    print_project_step(true, "macro.collect", milliseconds(report.timings.collect_ns));
    print_project_step(true, "macro.validate", milliseconds(report.timings.validate_ns));
    print_project_step(true, "macro.hygiene", milliseconds(report.timings.hygiene_ns));
    print_project_step(true, "macro.merge", milliseconds(report.timings.merge_ns));
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
    return {.config = parse_project_config(find_project_config(options.input)),
            .source_dir = source_dir_for_input(options.input)};
}

ProjectConfig config_for_options(const CliOptions& options) {
    ProjectConfig config = parse_project_config(find_project_config(options.input));
    if (!options.target_name.empty()) {
        config = apply_project_target(std::move(config), options.target_name);
    }
    if (options.project_driver) {
        ensure_project_dependencies(config, false, options.quiet);
    }
    return config;
}

FormatPathOptions format_options_for_project(const CliOptions& options) {
    FormatPathOptions out;
    if (!options.project_driver || !std::filesystem::is_directory(options.input)) {
        return out;
    }
    const std::filesystem::path config_path = find_project_config(options.input);
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

const ProjectIndex& checked_index(const CliOptions& options, const std::string& source,
                                  bool check_bodies) {
    const bool detail_output = !options.quiet && options.timings;
    print_project_step(detail_output, "config", options.input);
    const ProjectConfig config = config_for_options(options);
    print_project_step(detail_output, "parse", options.input);
    const bool merged_cpp_output = options.emit_cpp || options.header_output.has_value() ||
                                   options.c_header_output.has_value();
    const bool force_module_tree =
        !merged_cpp_output &&
        (options.emit_modules || options.expand_macros || options.project_driver);
    const std::filesystem::path source_dir = source_dir_for_input(options.input);
    const ProjectIndex& checked =
        cli_project_index_cache.get({.entry_path = options.input,
                                     .entry_source = source,
                                     .source_overrides = {},
                                     .config = config,
                                     .source_dir = source_dir,
                                     .build_values = options.build_values,
                                     .force_module_tree = force_module_tree,
                                     .include_native_headers = true,
                                     .check_semantics = true,
                                     .semantic_options = {.check_bodies = check_bodies}});
    print_project_step(detail_output, "indexed", options.input);
    print_project_step(detail_output, "checked", options.input);
    return checked;
}

const ProjectIndex& indexed_module_graph(const CliOptions& options, const std::string& source,
                                         bool check_semantics, bool check_bodies) {
    const bool detail_output = !options.quiet && options.timings;
    print_project_step(detail_output, "config", options.input);
    const ProjectConfig config = config_for_options(options);
    print_project_step(detail_output, "parse", options.input);
    const std::filesystem::path source_dir = source_dir_for_input(options.input);
    const ProjectIndex& index =
        cli_project_index_cache.get({.entry_path = options.input,
                                     .entry_source = source,
                                     .source_overrides = {},
                                     .config = config,
                                     .source_dir = source_dir,
                                     .build_values = options.build_values,
                                     .force_module_tree = true,
                                     .include_native_headers = true,
                                     .check_semantics = check_semantics,
                                     .semantic_options = {.check_bodies = check_bodies}});
    print_project_step(detail_output, "indexed", options.input);
    if (check_semantics) {
        print_project_step(detail_output, "checked", options.input);
    }
    return index;
}

const ModuleAst& checked_module(const CliOptions& options, const std::string& source,
                                bool check_bodies) {
    return checked_index(options, source, check_bodies).merged_module();
}

void check_source_file(CliOptions options, const std::filesystem::path& path) {
    options.input = path;
    (void)checked_index(options, read_required_text_file(path), true);
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

std::string stable_content_hash(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return std::to_string(hash);
}

std::string file_content_hash(const std::filesystem::path& path) {
    const std::optional<std::string> text = try_read_text_file(path);
    if (!text.has_value()) {
        return {};
    }
    return stable_content_hash(*text);
}

std::string file_state_stamp(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    const std::filesystem::path resolved = error ? path : canonical;
    const std::uintmax_t size = std::filesystem::file_size(resolved, error);
    if (error) {
        return {};
    }
    const auto write_time = std::filesystem::last_write_time(resolved, error);
    if (error) {
        return {};
    }
    std::ostringstream out;
    out << resolved.string() << '\t' << size << '\t' << file_time_stamp(write_time);
    return out.str();
}

bool artifact_manifest_current(const std::filesystem::path& dir,
                               const std::filesystem::path& manifest,
                               const std::filesystem::path& source_stamp,
                               const std::filesystem::path& compiler_path) {
    const std::optional<std::string> text = try_read_text_file(manifest);
    if (!text.has_value()) {
        return false;
    }
    const std::string expected_stamp_hash = file_content_hash(source_stamp);
    if (expected_stamp_hash.empty()) {
        return false;
    }
    const std::string expected_compiler_stamp = file_state_stamp(compiler_path);
    if (expected_compiler_stamp.empty()) {
        return false;
    }
    std::istringstream lines(*text);
    std::string line;
    const std::string source_stamp_prefix = "source-stamp\t";
    if (!std::getline(lines, line) || !line.starts_with(source_stamp_prefix) ||
        line.substr(source_stamp_prefix.size()) != expected_stamp_hash) {
        return false;
    }
    const std::string compiler_stamp_prefix = "compiler-stamp\t";
    if (!std::getline(lines, line) || !line.starts_with(compiler_stamp_prefix) ||
        line.substr(compiler_stamp_prefix.size()) != expected_compiler_stamp) {
        return false;
    }
    bool any = false;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            continue;
        }
        any = true;
        std::error_code error;
        if (!std::filesystem::exists(dir / line, error) || error) {
            return false;
        }
    }
    return any;
}

void write_artifact_manifest(const std::filesystem::path& manifest,
                             const std::filesystem::path& source_stamp,
                             const std::filesystem::path& compiler_path,
                             const std::vector<std::filesystem::path>& paths) {
    std::filesystem::create_directories(manifest.parent_path().empty() ? "."
                                                                       : manifest.parent_path());
    std::ofstream out(manifest, std::ios::binary);
    if (!out) {
        fail("could not open artifact manifest " + manifest.string());
    }
    const std::string source_stamp_hash = file_content_hash(source_stamp);
    if (source_stamp_hash.empty()) {
        fail("could not read source stamp " + source_stamp.string());
    }
    const std::string compiler_stamp = file_state_stamp(compiler_path);
    if (compiler_stamp.empty()) {
        fail("could not read compiler stamp " + compiler_path.string());
    }
    out << "source-stamp\t" << source_stamp_hash << '\n';
    out << "compiler-stamp\t" << compiler_stamp << '\n';
    for (const std::filesystem::path& path : paths) {
        out << path.string() << '\n';
    }
}

std::vector<std::string> all_project_module_paths(const ProjectIndex& index) {
    std::vector<std::string> out;
    out.reserve(index.modules().size());
    for (const ProjectModuleSummary& module : index.modules()) {
        out.push_back(module.module_path);
    }
    return out;
}

int run_build_command(const CliOptions& options, char* executable) {
    const ProjectConfig config = config_for_options(options);
    const std::filesystem::path dudu_executable = executable_path(executable);
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "backend", "cmake");
    print_project_step(project_output, "entry", options.input);
    print_project_step(project_output, "cmake", cmake_backend_log_source(config));
    print_project_step(project_output, "build", cmake_backend_log_build_dir(config));
    const std::filesystem::path bin = build_cmake_project({.config = config,
                                                           .input = options.input,
                                                           .output = options.output,
                                                           .dudu_executable = dudu_executable,
                                                           .stream_output = project_output,
                                                           .timings = options.timings,
                                                           .verbose = options.verbose});
    print_project_step(project_output, "output", bin);
    return 0;
}

int run_run_command(const CliOptions& options, char* executable) {
    const ProjectConfig config = config_for_options(options);
    const std::filesystem::path dudu_executable = executable_path(executable);
    if (config.target_kind != "executable") {
        fail("cannot run target kind: " + config.target_kind);
    }
    const bool project_output = options.project_driver && !options.quiet;
    print_project_step(project_output, "backend", "cmake");
    print_project_step(project_output, "entry", options.input);
    print_project_step(project_output, "cmake", cmake_backend_log_source(config));
    print_project_step(project_output, "build", cmake_backend_log_build_dir(config));
    const std::filesystem::path bin = build_cmake_project({.config = config,
                                                           .input = options.input,
                                                           .output = options.output,
                                                           .dudu_executable = dudu_executable,
                                                           .stream_output = project_output,
                                                           .timings = options.timings,
                                                           .verbose = options.verbose});
    const std::string run_command =
        append_command_args(shell_quote_path(bin), options.command_args);
    print_project_step(project_output, "run", run_command);
    return std::system(run_command.c_str()) == 0 ? 0 : 1;
}

int run_deps_fetch_command(const CliOptions& options) {
    const std::filesystem::path root =
        options.input.empty() ? std::filesystem::path(".") : options.input;
    ProjectConfig config = parse_project_config(find_project_config(root));
    ensure_project_dependencies(config, true, options.quiet);
    print_project_step(options.project_driver && !options.quiet, "deps", "fetched");
    return 0;
}

} // namespace

int run_cli(int argc, char** argv) {
    const std::string executable =
        argc > 0 ? std::filesystem::path(argv[0]).stem().string() : "duc";
    const bool project_driver = executable == "dudu";
    const CliOptions options = resolve_project_input(parse_cli_options(argc, argv, project_driver));
    set_project_step_timings(options.timings);
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
    if (options.deps_fetch) {
        return run_deps_fetch_command(options);
    }
    if (options.toolchain_update) {
        return run_toolchain_manager(executable_path(argv[0]), "update", options.command_args);
    }
    if (options.uninstall) {
        return run_toolchain_manager(executable_path(argv[0]), "uninstall", options.command_args);
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
                                  .timings = options.timings,
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

    std::optional<std::string> source;
    const auto input_source = [&]() -> const std::string& {
        if (!source.has_value()) {
            source = read_required_text_file(options.input);
        }
        return *source;
    };
    if (options.expand_macros) {
        const ProjectIndex& index = checked_index(options, input_source(), true);
        print_macro_performance(!options.quiet && options.timings, index.macro_report());
        write_text_output(options.output,
                          macro::render_expansion_report(
                              index.macro_report(), {.macro_filter = options.macro_filter,
                                                     .show_origins = options.show_macro_origins}));
        return 0;
    }
    if (options.header_output.has_value()) {
        write_text_output(options.header_output,
                          emit_cpp_header(checked_module(options, input_source(), false)));
        return 0;
    }
    if (options.c_header_output.has_value()) {
        write_text_output(options.c_header_output,
                          emit_c_header(checked_module(options, input_source(), false)));
        return 0;
    }
    if (options.emit_modules) {
        if (!options.output.has_value()) {
            fail("emit-modules requires -o <directory>");
        }
        const bool project_output = !options.quiet && (options.project_driver || options.timings);
        const std::filesystem::path compiler_path = executable_path(argv[0]);
        const std::filesystem::path stamp_file = *options.output / ".dudu_sources.stamp";
        const std::filesystem::path artifact_manifest = *options.output / ".dudu_artifacts.stamp";
        if (source_stamp_file_current_for_entry(stamp_file, options.input) &&
            artifact_manifest_current(*options.output, artifact_manifest, stamp_file,
                                      compiler_path)) {
            print_project_step(project_output, "dirty", "0 modules");
            print_project_step(project_output, "analyze", "0 modules");
            print_project_step(project_output, "emit", *options.output);
            return 0;
        }
        print_project_step(project_output, "load", options.input);
        const std::string& source = input_source();
        const ProjectIndex& index = indexed_module_graph(options, source, false, false);
        print_macro_performance(project_output && options.timings, index.macro_report());
        const std::vector<std::filesystem::path> changed_sources =
            index.changed_sources_since_stamp_file(stamp_file);
        std::vector<std::string> affected_modules =
            index.affected_modules_for_sources(changed_sources);
        if (changed_sources.empty() &&
            !artifact_manifest_current(*options.output, artifact_manifest, stamp_file,
                                       compiler_path)) {
            affected_modules = all_project_module_paths(index);
        }
        print_project_step(project_output, "dirty",
                           std::to_string(affected_modules.size()) + " modules");
        print_project_step(project_output, "analyze",
                           std::to_string(affected_modules.size()) + " modules");
        const Clock::time_point sema_start = Clock::now();
        analyze_module_tree(index.merged_module(), affected_modules, {.check_bodies = true});
        if (project_output && options.timings && index.macro_report().invocations != 0) {
            print_project_step(true, "macro.generated_sema", milliseconds(elapsed_ns(sema_start)));
        }
        print_project_step(project_output, "emit", *options.output);
        const Clock::time_point codegen_start = Clock::now();
        write_cpp_artifacts(*options.output,
                            emit_cpp_module_artifacts(index.merged_module(), affected_modules));
        if (project_output && options.timings && index.macro_report().invocations != 0) {
            print_project_step(true, "macro.generated_codegen",
                               milliseconds(elapsed_ns(codegen_start)));
        }
        index.write_source_stamp_file(stamp_file);
        write_artifact_manifest(artifact_manifest, stamp_file, compiler_path,
                                cpp_module_artifact_paths(index.merged_module()));
        return 0;
    }
    if (options.emit_test_modules) {
        if (!options.output.has_value()) {
            fail("emit-test-modules requires -o <directory>");
        }
        const bool project_output = !options.quiet && (options.project_driver || options.timings);
        const std::filesystem::path compiler_path = executable_path(argv[0]);
        const std::filesystem::path stamp_file = *options.output / ".dudu_test_sources.stamp";
        const std::filesystem::path artifact_manifest =
            *options.output / ".dudu_test_artifacts.stamp";
        if (source_stamp_file_current_for_entry(stamp_file, options.input) &&
            artifact_manifest_current(*options.output, artifact_manifest, stamp_file,
                                      compiler_path)) {
            print_project_step(project_output, "dirty", "0 modules");
            print_project_step(project_output, "analyze", "0 modules");
            print_project_step(project_output, "emit", *options.output);
            return 0;
        }
        print_project_step(project_output, "load", options.input);
        const std::string& source = input_source();
        const ProjectIndex& index = indexed_module_graph(options, source, false, false);
        const std::vector<std::filesystem::path> changed_sources =
            index.changed_sources_since_stamp_file(stamp_file);
        std::vector<std::string> affected_modules =
            index.affected_modules_for_sources(changed_sources);
        if (changed_sources.empty() &&
            !artifact_manifest_current(*options.output, artifact_manifest, stamp_file,
                                       compiler_path)) {
            affected_modules = all_project_module_paths(index);
        }
        print_project_step(project_output, "dirty",
                           std::to_string(affected_modules.size()) + " modules");
        print_project_step(project_output, "analyze",
                           std::to_string(affected_modules.size()) + " modules");
        analyze_module_tree(index.merged_module(), affected_modules, {.check_bodies = true});
        print_project_step(project_output, "emit", *options.output);
        write_cpp_artifacts(*options.output, emit_cpp_test_module_artifacts(
                                                 index.merged_module(), affected_modules,
                                                 options.test_filter, !options.no_capture));
        index.write_source_stamp_file(stamp_file);
        write_artifact_manifest(artifact_manifest, stamp_file, compiler_path,
                                cpp_test_module_artifact_paths(index.merged_module()));
        return 0;
    }
    if (options.emit_cpp) {
        write_text_output(options.output,
                          emit_cpp_source(checked_module(options, input_source(), true)));
        return 0;
    }
    write_text_output(std::nullopt, emit_cpp_source(checked_module(options, input_source(), true)));
    return 0;
}

} // namespace dudu
