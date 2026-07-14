#include "dudu/macro/macro_worker_build.hpp"

#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/macro/macro_hash.hpp"
#include "dudu/macro/macro_protocol_generated.hpp"
#include "dudu/macro/macro_worker_codegen.hpp"
#include "dudu/macro/macro_worker_compile.hpp"
#include "dudu/sema/sema.hpp"

#include <algorithm>
#include <atomic>
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

std::atomic<std::uint64_t> staging_counter{0};

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
    hash.add(options.project_root.generic_string());
    hash.add(options.compiler);
    hash.add(options.cpp_standard);
    hash.add(options.toolchain_identity);
    hash.add(options.dudu_toolchain_identity);
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
    hash.add(options.sdk_bridge_source.generic_string());
    hash.add(try_read_text_file(options.sdk_bridge_source).value_or(""));
    add_strings(options.defines);
    add_strings(options.compiler_flags);
    add_strings(options.libraries);
    add_strings(options.linker_flags);
    add_strings(options.capabilities);
    add_strings(options.non_cacheable_macros);
    return hash.finish();
}

std::optional<std::string> source_identity(const ModuleAst& module,
                                           const std::vector<std::string>& selected,
                                           const Plan& plan, const WorkerBuildOptions& options) {
    const auto modules = unit_map(module);
    StableHash hash;
    hash.add("dudu-macro-worker-source-v1");
    hash.add(std::to_string(protocol::protocol_version));
    hash.add(std::to_string(protocol::schema_version));
    hash.add(options.package);
    hash.add(options.project_root.generic_string());
    hash.add(options.compiler);
    hash.add(options.cpp_standard);
    hash.add(options.toolchain_identity);
    hash.add(options.dudu_toolchain_identity);
    for (const std::string& module_path : selected) {
        const ModuleAst& unit = *modules.at(module_path);
        if (unit.source_digest.empty())
            return std::nullopt;
        hash.add(module_path);
        hash.add(unit.source_path.generic_string());
        hash.add(unit.source_digest);
        for (const auto& [name, value] : unit.build_values) {
            hash.add(name);
            hash.add(value);
        }
    }
    for (const auto& [identity, definition] : plan.definitions) {
        hash.add(identity);
        hash.add(std::to_string(static_cast<int>(definition.accepted_kind)));
    }
    const auto add_paths = [&](const auto& values) {
        for (const auto& value : values)
            hash.add(std::filesystem::path(value).generic_string());
    };
    const auto add_strings = [&](const auto& values) {
        for (const auto& value : values)
            hash.add(value);
    };
    add_paths(options.runtime_include_dirs);
    add_paths(options.include_dirs);
    add_paths(options.library_dirs);
    add_paths(options.cpp_sources);
    hash.add(options.runtime_library.generic_string());
    hash.add(options.sdk_bridge_source.generic_string());
    hash.add(try_read_text_file(options.sdk_bridge_source).value_or(""));
    add_strings(options.defines);
    add_strings(options.compiler_flags);
    add_strings(options.libraries);
    add_strings(options.linker_flags);
    add_strings(options.capabilities);
    add_strings(options.non_cacheable_macros);
    return hash.finish();
}

std::optional<std::string> read_worker_lookup(const std::filesystem::path& path) {
    const std::optional<std::string> value = try_read_text_file(path);
    if (!value || value->empty())
        return std::nullopt;
    std::string identity = *value;
    while (!identity.empty() && (identity.back() == '\n' || identity.back() == '\r'))
        identity.pop_back();
    return identity.empty() ? std::nullopt : std::optional<std::string>{std::move(identity)};
}

void write_text(const std::filesystem::path& path, const std::string& text);

void write_worker_lookup(const std::filesystem::path& path, const std::string& identity) {
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path temporary =
        path.string() + ".tmp." + std::to_string(::getpid()) + "." +
        std::to_string(staging_counter.fetch_add(1, std::memory_order_relaxed));
    write_text(temporary, identity + "\n");
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("could not publish macro worker lookup: " + error.message());
    }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not write macro worker source: " + path.string());
    }
    output << text;
}

} // namespace

WorkerBinary build_worker_binary(const ModuleAst& module, const Plan& plan,
                                 const WorkerBuildOptions& options) {
    if (options.cache_dir.empty()) {
        throw std::invalid_argument("macro worker cache directory is required");
    }
    const std::vector<std::string> selected = dependency_closure(module, plan);
    const std::optional<std::string> source_key = source_identity(module, selected, plan, options);
    const std::filesystem::path lookup =
        source_key ? options.cache_dir / "lookup" / *source_key : std::filesystem::path{};
    if (source_key) {
        if (const std::optional<std::string> identity = read_worker_lookup(lookup)) {
            const std::filesystem::path executable = options.cache_dir / *identity / "worker";
            if (std::filesystem::is_regular_file(executable)) {
                return {.executable = executable,
                        .working_directory = options.project_root,
                        .identity = *identity,
                        .cache_hit = true};
            }
        }
    }
    analyze_module_tree(module, selected,
                        {.check_bodies = true, .include_macro_host_modules = true});
    const std::vector<CppModuleArtifact> artifacts =
        emit_cpp_module_artifacts(module, selected, {.include_macro_host_modules = true});
    const std::string identity = build_identity(artifacts, plan, options);
    const std::filesystem::path cache_entry = options.cache_dir / identity;
    const std::filesystem::path executable = cache_entry / "worker";
    if (std::filesystem::is_regular_file(executable)) {
        if (source_key)
            write_worker_lookup(lookup, identity);
        return {.executable = executable,
                .working_directory = options.project_root,
                .identity = identity,
                .cache_hit = true};
    }

    std::filesystem::create_directories(options.cache_dir);
    const std::filesystem::path staging =
        options.cache_dir /
        (identity + ".tmp." + std::to_string(::getpid()) + "." +
         std::to_string(staging_counter.fetch_add(1, std::memory_order_relaxed)));
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    write_cpp_artifacts(staging, artifacts);
    write_text(
        staging / "worker.cpp",
        generate_worker_source(plan, {.package = options.package,
                                      .binary_identity = identity,
                                      .project_root = options.project_root.generic_string(),
                                      .capabilities = options.capabilities,
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
    if (source_key)
        write_worker_lookup(lookup, identity);
    return {.executable = executable,
            .working_directory = options.project_root,
            .identity = identity,
            .cache_hit = false};
}

} // namespace dudu::macro
