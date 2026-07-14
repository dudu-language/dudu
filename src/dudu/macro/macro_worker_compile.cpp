#include "dudu/macro/macro_worker_compile.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/macro/macro_hash.hpp"
#include "dudu/native/native_build.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace dudu::macro {
namespace {

std::atomic<std::uint64_t> sdk_staging_counter{0};

void write_text(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("could not write macro SDK source: " + path.string());
    }
    output << content;
}

const CppModuleArtifact& require_artifact(const std::vector<CppModuleArtifact>& artifacts,
                                          const std::filesystem::path& path) {
    const auto found = std::find_if(artifacts.begin(), artifacts.end(),
                                    [&](const auto& artifact) { return artifact.path == path; });
    if (found == artifacts.end()) {
        throw std::runtime_error("macro SDK artifact is missing: " + path.generic_string());
    }
    return *found;
}

std::string sdk_identity(const std::vector<CppModuleArtifact>& artifacts,
                         const WorkerBuildOptions& options) {
    StableHash hash;
    hash.add("dudu-macro-sdk-v1");
    hash.add(options.compiler);
    hash.add(options.cpp_standard);
    hash.add(options.toolchain_identity);
    hash.add(options.dudu_toolchain_identity);
    hash.add(options.sdk_bridge_source.generic_string());
    hash.add(try_read_text_file(options.sdk_bridge_source).value_or(""));
    for (const std::filesystem::path& path :
         {std::filesystem::path("dudu_runtime.hpp"), std::filesystem::path("dudu/ast.hpp"),
          std::filesystem::path("dudu/ast.cpp")}) {
        const CppModuleArtifact& artifact = require_artifact(artifacts, path);
        hash.add(artifact.path.generic_string());
        hash.add(artifact.content);
    }
    for (const std::filesystem::path& include : options.runtime_include_dirs) {
        hash.add(include.generic_string());
    }
    for (const std::string& define : options.defines) {
        hash.add(define);
    }
    for (const std::string& flag : options.compiler_flags) {
        hash.add(flag);
    }
    return hash.finish();
}

std::string compile_prefix(const WorkerBuildOptions& options,
                           const std::filesystem::path& generated_include,
                           std::string_view optimization) {
    std::string command = shell_quote_arg(options.compiler) +
                          " -std=" + shell_quote_arg(options.cpp_standard) + " " +
                          std::string(optimization);
    command += " -I" + shell_quote_path(generated_include);
    for (const std::filesystem::path& include : options.runtime_include_dirs) {
        command += " -I" + shell_quote_path(include);
    }
    for (const std::filesystem::path& include : options.include_dirs) {
        command += " -I" + shell_quote_path(include);
    }
    for (const std::string& define : options.defines) {
        command += " -D" + shell_quote_arg(define);
    }
    for (const std::string& flag : options.compiler_flags) {
        command += " " + shell_quote_arg(flag);
    }
    command += " -include dudu/macro/macro_capabilities.hpp";
    return command;
}

std::filesystem::path prepare_sdk(const std::vector<CppModuleArtifact>& artifacts,
                                  const WorkerBuildOptions& options) {
    if (options.sdk_cache_dir.empty()) {
        throw std::invalid_argument("macro SDK cache directory is required");
    }
    const std::string identity = sdk_identity(artifacts, options);
    const std::filesystem::path entry = options.sdk_cache_dir / identity;
    const std::filesystem::path object = entry / "dudu_ast.o";
    const std::filesystem::path bridge_object = entry / "dudu_sdk_bridge.o";
    if (std::filesystem::is_regular_file(object) &&
        std::filesystem::is_regular_file(bridge_object)) {
        return entry;
    }
    std::filesystem::create_directories(options.sdk_cache_dir);
    const std::filesystem::path staging =
        options.sdk_cache_dir /
        (identity + ".tmp." + std::to_string(::getpid()) + "." +
         std::to_string(sdk_staging_counter.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(staging);
    for (const std::filesystem::path& path :
         {std::filesystem::path("dudu_runtime.hpp"), std::filesystem::path("dudu/ast.hpp"),
          std::filesystem::path("dudu/ast.cpp")}) {
        const CppModuleArtifact& artifact = require_artifact(artifacts, path);
        write_text(staging / path, artifact.content);
    }
    const std::optional<std::string> bridge_source = try_read_text_file(options.sdk_bridge_source);
    if (!bridge_source) {
        std::filesystem::remove_all(staging);
        throw std::runtime_error("could not read macro SDK bridge source: " +
                                 options.sdk_bridge_source.string());
    }
    write_text(staging / "macro_sdk_bridge_generated.cpp", *bridge_source);

    const std::string ast_command = compile_prefix(options, staging, "-O2") + " -c " +
                                    shell_quote_path(staging / "dudu/ast.cpp") + " -o " +
                                    shell_quote_path(staging / "dudu_ast.o");
    if (run_shell_command(ast_command, staging / "ast.log") != 0) {
        const std::optional<std::string> detail = try_read_text_file(staging / "ast.log");
        std::filesystem::remove_all(staging);
        throw std::runtime_error("could not compile macro SDK\ncommand: " + ast_command + "\n" +
                                 detail.value_or("macro SDK compiler failed"));
    }
    const std::string bridge_command =
        compile_prefix(options, staging, "-O2") + " -c " +
        shell_quote_path(staging / "macro_sdk_bridge_generated.cpp") + " -o " +
        shell_quote_path(staging / "dudu_sdk_bridge.o");
    if (run_shell_command(bridge_command, staging / "bridge.log") != 0) {
        const std::optional<std::string> detail = try_read_text_file(staging / "bridge.log");
        std::filesystem::remove_all(staging);
        throw std::runtime_error("could not compile macro SDK bridge\ncommand: " + bridge_command +
                                 "\n" + detail.value_or("macro SDK bridge compiler failed"));
    }

    std::error_code error;
    std::filesystem::rename(staging, entry, error);
    if (error && (!std::filesystem::is_regular_file(object) ||
                  !std::filesystem::is_regular_file(bridge_object))) {
        std::filesystem::remove_all(staging);
        throw std::runtime_error("could not publish macro SDK cache entry: " + error.message());
    }
    if (error) {
        std::filesystem::remove_all(staging);
    }
    return entry;
}

struct CompileUnit {
    std::filesystem::path source;
    std::filesystem::path object;
    std::filesystem::path log;
    std::string_view optimization;
};

std::vector<CompileUnit> compile_units(const std::filesystem::path& dir,
                                       const std::vector<CppModuleArtifact>& artifacts,
                                       const WorkerBuildOptions& options) {
    std::vector<std::filesystem::path> sources = {dir / "worker.cpp"};
    for (const CppModuleArtifact& artifact : artifacts) {
        if (artifact.kind == CppModuleArtifactKind::Source && artifact.module_path != "dudu.ast") {
            sources.push_back(dir / artifact.path);
        }
    }
    sources.insert(sources.end(), options.cpp_sources.begin(), options.cpp_sources.end());

    const std::filesystem::path object_dir = dir / "objects";
    std::filesystem::create_directories(object_dir);
    std::vector<CompileUnit> units;
    units.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const std::string stem = "unit_" + std::to_string(index);
        units.push_back({.source = sources[index],
                         .object = object_dir / (stem + ".o"),
                         .log = object_dir / (stem + ".log"),
                         .optimization = index == 0 ? "-O1" : "-O2"});
    }
    return units;
}

void compile_units_parallel(const std::filesystem::path& dir, const WorkerBuildOptions& options,
                            const std::vector<CompileUnit>& units) {
    std::vector<int> statuses(units.size(), 0);
    std::vector<std::string> commands(units.size());
    std::atomic<std::size_t> next{0};
    const unsigned available = std::max(1U, std::thread::hardware_concurrency());
    const std::size_t worker_count = std::min<std::size_t>(available, units.size());
    std::vector<std::jthread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= units.size()) {
                    return;
                }
                const CompileUnit& unit = units[index];
                commands[index] = compile_prefix(options, dir, unit.optimization) + " -c " +
                                  shell_quote_path(unit.source) + " -o " +
                                  shell_quote_path(unit.object);
                statuses[index] = run_shell_command(commands[index], unit.log);
            }
        });
    }
    workers.clear();

    for (std::size_t index = 0; index < units.size(); ++index) {
        if (statuses[index] == 0) {
            continue;
        }
        const std::optional<std::string> detail = try_read_text_file(units[index].log);
        throw std::runtime_error(
            "could not compile macro worker source\ncommand: " + commands[index] + "\n" +
            detail.value_or("macro worker compiler failed"));
    }
}

void link_worker(const std::filesystem::path& dir, const std::filesystem::path& sdk,
                 const WorkerBuildOptions& options, const std::vector<CompileUnit>& units) {
    std::string command = shell_quote_arg(options.compiler);
    for (const CompileUnit& unit : units) {
        command += " " + shell_quote_path(unit.object);
    }
    command += " " + shell_quote_path(sdk / "dudu_ast.o");
    command += " " + shell_quote_path(sdk / "dudu_sdk_bridge.o");
    if (!options.runtime_library.empty()) {
        command += " " + shell_quote_path(options.runtime_library);
    }
    for (const std::filesystem::path& library_dir : options.library_dirs) {
        command += " -L" + shell_quote_path(library_dir);
    }
    for (const std::string& flag : options.compiler_flags) {
        command += " " + shell_quote_arg(flag);
    }
    for (const std::string& library : options.libraries) {
        command += " -l" + shell_quote_arg(library);
    }
    for (const std::string& flag : options.linker_flags) {
        command += " " + shell_quote_arg(flag);
    }
    command += " -o " + shell_quote_path(dir / "worker");
    const std::filesystem::path log = dir / "link.log";
    if (run_shell_command(command, log) != 0) {
        const std::optional<std::string> detail = try_read_text_file(log);
        throw std::runtime_error("could not link macro worker\ncommand: " + command + "\n" +
                                 detail.value_or("macro worker linker failed"));
    }
}

} // namespace

void compile_worker(const std::filesystem::path& dir,
                    const std::vector<CppModuleArtifact>& artifacts,
                    const WorkerBuildOptions& options) {
    const std::filesystem::path sdk = prepare_sdk(artifacts, options);
    const std::vector<CompileUnit> units = compile_units(dir, artifacts, options);
    compile_units_parallel(dir, options, units);
    link_worker(dir, sdk, options, units);
}

} // namespace dudu::macro
