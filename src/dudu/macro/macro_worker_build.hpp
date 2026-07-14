#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/macro/macro_registry.hpp"

#include <filesystem>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace dudu::macro {

struct WorkerBuildOptions {
    std::filesystem::path cache_dir;
    std::filesystem::path sdk_cache_dir;
    std::filesystem::path project_root;
    std::string package;
    std::string compiler = "c++";
    std::string cpp_standard = "c++20";
    std::string toolchain_identity;
    std::string dudu_toolchain_identity;
    std::vector<std::filesystem::path> runtime_include_dirs;
    std::filesystem::path runtime_library;
    std::filesystem::path sdk_bridge_source;
    std::vector<std::filesystem::path> include_dirs;
    std::vector<std::filesystem::path> library_dirs;
    std::vector<std::filesystem::path> cpp_sources;
    std::vector<std::string> defines;
    std::vector<std::string> compiler_flags;
    std::vector<std::string> libraries;
    std::vector<std::string> linker_flags;
    std::vector<std::string> capabilities;
    std::set<std::string> non_cacheable_macros;
};

struct WorkerBinary {
    struct Timings {
        std::uint64_t sdk_prepare_ns = 0;
        std::uint64_t compile_ns = 0;
        std::uint64_t link_ns = 0;
    };

    std::filesystem::path executable;
    std::filesystem::path working_directory;
    std::string identity;
    bool cache_hit = false;
    Timings timings;
};

WorkerBinary build_worker_binary(const ModuleAst& module, const Plan& plan,
                                 const WorkerBuildOptions& options);

} // namespace dudu::macro
