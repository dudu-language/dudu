#include "dudu/macro/macro_worker_build.hpp"

#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/macro/macro_hash.hpp"
#include "dudu/macro/macro_protocol_generated.hpp"
#include "dudu/macro/macro_worker_codegen.hpp"
#include "dudu/native/native_build.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace dudu::macro {
namespace {

std::vector<const ModuleAst*> units(const ModuleAst& module) {
    if (module.module_units.empty()) {
        return {&module};
    }
    std::vector<const ModuleAst*> out;
    out.reserve(module.module_units.size());
    for (const ModuleAst& unit : module.module_units) {
        out.push_back(&unit);
    }
    return out;
}

std::map<std::string, const ModuleAst*> unit_map(const ModuleAst& module) {
    std::map<std::string, const ModuleAst*> out;
    for (const ModuleAst* unit : units(module)) {
        out.emplace(unit->module_path, unit);
    }
    return out;
}

std::vector<std::string> dependency_closure(const ModuleAst& module, const Plan& plan) {
    const auto modules = unit_map(module);
    std::set<std::string> pending;
    std::set<std::string> selected;
    for (const auto& [_, definition] : plan.definitions) {
        pending.insert(definition.module_path);
    }
    if (modules.contains("dudu.ast")) {
        pending.insert("dudu.ast");
    }
    while (!pending.empty()) {
        const std::string path = *pending.begin();
        pending.erase(pending.begin());
        if (!selected.insert(path).second) {
            continue;
        }
        const auto found = modules.find(path);
        if (found == modules.end()) {
            throw std::runtime_error("macro worker dependency module is missing: " + path);
        }
        for (const ModuleDependency& dependency : found->second->dependencies) {
            if (modules.contains(dependency.resolved_module_path) &&
                !selected.contains(dependency.resolved_module_path)) {
                pending.insert(dependency.resolved_module_path);
            }
        }
    }
    return {selected.begin(), selected.end()};
}

std::string build_identity(const std::vector<CppModuleArtifact>& artifacts, const Plan& plan,
                           const WorkerBuildOptions& options) {
    StableHash hash;
    hash.add("dudu-macro-worker-binary-v1");
    hash.add(std::to_string(protocol::protocol_version));
    hash.add(std::to_string(protocol::schema_version));
    hash.add(options.package);
    hash.add(options.compiler);
    hash.add(options.cpp_standard);
    hash.add(options.toolchain_identity);
    for (const auto& [identity, definition] : plan.definitions) {
        hash.add(identity);
        hash.add(std::to_string(static_cast<int>(definition.accepted_kind)));
    }
    for (const CppModuleArtifact& artifact : artifacts) {
        hash.add(artifact.path.generic_string());
        hash.add(artifact.content);
    }
    const auto add_paths = [&](const auto& values) {
        for (const auto& value : values) {
            hash.add(std::filesystem::path(value).generic_string());
        }
    };
    const auto add_strings = [&](const auto& values) {
        for (const auto& value : values) {
            hash.add(value);
        }
    };
    add_paths(options.runtime_include_dirs);
    add_paths(options.include_dirs);
    add_paths(options.library_dirs);
    add_paths(options.cpp_sources);
    hash.add(options.runtime_library.generic_string());
    add_strings(options.defines);
    add_strings(options.compiler_flags);
    add_strings(options.libraries);
    add_strings(options.linker_flags);
    add_strings(options.capabilities);
    add_strings(options.non_cacheable_macros);
    return hash.finish();
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not write macro worker source: " + path.string());
    }
    output << text;
}

std::string compile_command(const std::filesystem::path& dir,
                            const std::vector<CppModuleArtifact>& artifacts,
                            const WorkerBuildOptions& options) {
    std::string command = shell_quote_arg(options.compiler) + " -std=" +
                          shell_quote_arg(options.cpp_standard) + " -O2";
    command += " -I" + shell_quote_path(dir);
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
    command += " " + shell_quote_path(dir / "worker.cpp");
    for (const CppModuleArtifact& artifact : artifacts) {
        if (artifact.kind == CppModuleArtifactKind::Source) {
            command += " " + shell_quote_path(dir / artifact.path);
        }
    }
    for (const std::filesystem::path& source : options.cpp_sources) {
        command += " " + shell_quote_path(source);
    }
    if (!options.runtime_library.empty()) {
        command += " " + shell_quote_path(options.runtime_library);
    }
    for (const std::filesystem::path& library_dir : options.library_dirs) {
        command += " -L" + shell_quote_path(library_dir);
    }
    for (const std::string& library : options.libraries) {
        command += " -l" + shell_quote_arg(library);
    }
    for (const std::string& flag : options.linker_flags) {
        command += " " + shell_quote_arg(flag);
    }
    command += " -o " + shell_quote_path(dir / "worker");
    return command;
}

void compile_worker(const std::filesystem::path& dir,
                    const std::vector<CppModuleArtifact>& artifacts,
                    const WorkerBuildOptions& options) {
    const std::filesystem::path log = dir / "build.log";
    const std::string command = compile_command(dir, artifacts, options);
    if (run_shell_command(command, log) != 0) {
        const std::optional<std::string> detail = try_read_text_file(log);
        throw std::runtime_error("could not compile macro worker\ncommand: " + command +
                                 "\n" + detail.value_or("macro worker compiler failed"));
    }
}

} // namespace

WorkerBinary build_worker_binary(const ModuleAst& module, const Plan& plan,
                                 const WorkerBuildOptions& options) {
    if (options.cache_dir.empty()) {
        throw std::invalid_argument("macro worker cache directory is required");
    }
    const std::vector<std::string> selected = dependency_closure(module, plan);
    const std::vector<CppModuleArtifact> artifacts =
        emit_cpp_module_artifacts(module, selected);
    const std::string identity = build_identity(artifacts, plan, options);
    const std::filesystem::path cache_entry = options.cache_dir / identity;
    const std::filesystem::path executable = cache_entry / "worker";
    if (std::filesystem::is_regular_file(executable)) {
        return {.executable = executable, .identity = identity, .cache_hit = true};
    }

    std::filesystem::create_directories(options.cache_dir);
    const std::filesystem::path staging = options.cache_dir /
                                          (identity + ".tmp." + std::to_string(::getpid()));
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    write_cpp_artifacts(staging, artifacts);
    write_text(staging / "worker.cpp",
               generate_worker_source(plan,
                                      {.package = options.package,
                                       .binary_identity = identity,
                                       .non_cacheable_macros = options.non_cacheable_macros}));
    try {
        compile_worker(staging, artifacts, options);
        std::error_code error;
        std::filesystem::rename(staging, cache_entry, error);
        if (error && !std::filesystem::is_regular_file(executable)) {
            throw std::runtime_error("could not publish macro worker cache entry: " +
                                     error.message());
        }
        if (error) {
            std::filesystem::remove_all(staging);
        }
    } catch (...) {
        std::filesystem::remove_all(staging);
        throw;
    }
    return {.executable = executable, .identity = identity, .cache_hit = false};
}

} // namespace dudu::macro
