#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/frontend/cli_command_internal.hpp"
#include "dudu/macro/macro_expansion_render.hpp"
#include "dudu/project/project_driver.hpp"
#include "dudu/project/project_index_cache.hpp"
#include "dudu/sema/sema.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
    if (!enabled || (report.invocations == 0 && report.definitions.empty())) {
        return;
    }
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

const ProjectIndex& checked_index(const CliOptions& options, const std::string& source,
                                  bool check_bodies) {
    const bool detail_output = !options.quiet && options.timings;
    print_project_step(detail_output, "config", options.input);
    const ProjectConfig config = cli_project_config(options);
    print_project_step(detail_output, "parse", options.input);
    const bool merged_cpp_output = options.emit_cpp || options.header_output.has_value() ||
                                   options.c_header_output.has_value();
    const bool force_module_tree =
        !merged_cpp_output &&
        (options.emit_modules || options.expand_macros || options.project_driver);
    const ProjectIndex& checked =
        cli_project_index_cache.get({.entry_path = options.input,
                                     .entry_source = source,
                                     .source_overrides = {},
                                     .config = config,
                                     .source_dir = cli_source_dir_for_input(options.input),
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
    const ProjectConfig config = cli_project_config(options);
    print_project_step(detail_output, "parse", options.input);
    const ProjectIndex& index =
        cli_project_index_cache.get({.entry_path = options.input,
                                     .entry_source = source,
                                     .source_overrides = {},
                                     .config = config,
                                     .source_dir = cli_source_dir_for_input(options.input),
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

void check_source_path(const CliOptions& options) {
    if (!std::filesystem::is_directory(options.input)) {
        check_source_file(options, options.input);
        return;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(options.input)) {
        if (entry.is_regular_file() && entry.path().extension() == ".dd") {
            check_source_file(options, entry.path());
        }
    }
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
    return text.has_value() ? stable_content_hash(*text) : std::string{};
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
    const std::string source_hash = file_content_hash(source_stamp);
    const std::string compiler_stamp = file_state_stamp(compiler_path);
    if (source_hash.empty() || compiler_stamp.empty()) {
        return false;
    }

    std::istringstream lines(*text);
    std::string line;
    constexpr std::string_view source_prefix = "source-stamp\t";
    if (!std::getline(lines, line) || !line.starts_with(source_prefix) ||
        line.substr(source_prefix.size()) != source_hash) {
        return false;
    }
    constexpr std::string_view compiler_prefix = "compiler-stamp\t";
    if (!std::getline(lines, line) || !line.starts_with(compiler_prefix) ||
        line.substr(compiler_prefix.size()) != compiler_stamp) {
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
    const std::string source_hash = file_content_hash(source_stamp);
    const std::string compiler_stamp = file_state_stamp(compiler_path);
    if (source_hash.empty()) {
        fail("could not read source stamp " + source_stamp.string());
    }
    if (compiler_stamp.empty()) {
        fail("could not read compiler stamp " + compiler_path.string());
    }
    out << "source-stamp\t" << source_hash << '\n';
    out << "compiler-stamp\t" << compiler_stamp << '\n';
    for (const std::filesystem::path& path : paths) {
        out << path.string() << '\n';
    }
}

std::vector<std::string> all_project_module_paths(const ProjectIndex& index) {
    std::vector<std::string> paths;
    paths.reserve(index.modules().size());
    for (const ProjectModuleSummary& module : index.modules()) {
        paths.push_back(module.module_path);
    }
    return paths;
}

std::vector<std::string> affected_modules(const ProjectIndex& index,
                                          const std::filesystem::path& stamp_file,
                                          const std::filesystem::path& output_dir,
                                          const std::filesystem::path& artifact_manifest,
                                          const std::filesystem::path& compiler_path) {
    const std::vector<std::filesystem::path> changed_sources =
        index.changed_sources_since_stamp_file(stamp_file);
    std::vector<std::string> affected = index.affected_modules_for_sources(changed_sources);
    if (changed_sources.empty() &&
        !artifact_manifest_current(output_dir, artifact_manifest, stamp_file, compiler_path)) {
        return all_project_module_paths(index);
    }
    return affected;
}

bool skip_current_module_emission(const CliOptions& options,
                                  const std::filesystem::path& output_dir,
                                  const std::filesystem::path& stamp_file,
                                  const std::filesystem::path& artifact_manifest,
                                  const std::filesystem::path& compiler_path) {
    if (!source_stamp_file_current_for_entry(stamp_file, options.input) ||
        !artifact_manifest_current(output_dir, artifact_manifest, stamp_file, compiler_path)) {
        return false;
    }
    const bool project_output = !options.quiet;
    print_project_step(project_output, "dirty", "0 modules");
    print_project_step(project_output, "analyze", "0 modules");
    print_project_step(project_output, "emit", output_dir);
    return true;
}

int emit_modules(const CliOptions& options, char* executable, bool tests) {
    if (!options.output.has_value()) {
        fail(std::string(tests ? "emit-test-modules" : "emit-modules") +
             " requires -o <directory>");
    }
    const bool project_output = !options.quiet;
    const std::filesystem::path compiler_path = cli_executable_path(executable);
    const std::filesystem::path stamp_file =
        *options.output / (tests ? ".dudu_test_sources.stamp" : ".dudu_sources.stamp");
    const std::filesystem::path artifact_manifest =
        *options.output / (tests ? ".dudu_test_artifacts.stamp" : ".dudu_artifacts.stamp");
    if (skip_current_module_emission(options, *options.output, stamp_file, artifact_manifest,
                                     compiler_path)) {
        return 0;
    }

    print_project_step(project_output, "load", options.input);
    const std::string source = read_required_text_file(options.input);
    const ProjectIndex& index = indexed_module_graph(options, source, false, false);
    if (!tests) {
        print_macro_performance(project_output && options.timings, index.macro_report());
    }
    const std::vector<std::string> affected =
        affected_modules(index, stamp_file, *options.output, artifact_manifest, compiler_path);
    print_project_step(project_output, "dirty", std::to_string(affected.size()) + " modules");
    print_project_step(project_output, "analyze", std::to_string(affected.size()) + " modules");

    const Clock::time_point sema_start = Clock::now();
    analyze_module_tree(index.merged_module(), affected, {.check_bodies = true});
    if (!tests && project_output && options.timings && index.macro_report().invocations != 0) {
        print_project_step(true, "macro.generated_sema", milliseconds(elapsed_ns(sema_start)));
    }

    print_project_step(project_output, "emit", *options.output);
    const Clock::time_point codegen_start = Clock::now();
    if (tests) {
        write_cpp_artifacts(*options.output, emit_cpp_test_module_artifacts(
                                                 index.merged_module(), affected,
                                                 options.test_filter, !options.no_capture));
    } else {
        write_cpp_artifacts(*options.output,
                            emit_cpp_module_artifacts(index.merged_module(), affected));
    }
    if (!tests && project_output && options.timings && index.macro_report().invocations != 0) {
        print_project_step(true, "macro.generated_codegen",
                           milliseconds(elapsed_ns(codegen_start)));
    }

    index.write_source_stamp_file(stamp_file);
    write_artifact_manifest(artifact_manifest, stamp_file, compiler_path,
                            tests ? cpp_test_module_artifact_paths(index.merged_module())
                                  : cpp_module_artifact_paths(index.merged_module()));
    return 0;
}

} // namespace

int run_compile_command(const CliOptions& options, char* executable) {
    if (options.check) {
        print_project_step(options.project_driver && !options.quiet, "check", options.input);
        check_source_path(options);
        print_project_step(options.project_driver && !options.quiet, "ok", options.input);
        return 0;
    }

    if (options.emit_modules) {
        return emit_modules(options, executable, false);
    }
    if (options.emit_test_modules) {
        return emit_modules(options, executable, true);
    }

    const std::string source = read_required_text_file(options.input);
    if (options.expand_macros) {
        const ProjectIndex& index = checked_index(options, source, true);
        print_macro_performance(!options.quiet && options.timings, index.macro_report());
        write_text_output(options.output,
                          macro::render_expansion_report(
                              index.macro_report(), {.macro_filter = options.macro_filter,
                                                     .show_origins = options.show_macro_origins}));
        return 0;
    }
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
    if (options.emit_cpp) {
        write_text_output(options.output, emit_cpp_source(checked_module(options, source, true)));
        return 0;
    }
    write_text_output(std::nullopt, emit_cpp_source(checked_module(options, source, true)));
    return 0;
}

} // namespace dudu
