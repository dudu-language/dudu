#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/macro/macro_protocol_generated.hpp"
#include "dudu/project/project_config.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dudu::macro {

struct ExpansionOptions {
    ProjectConfig project;
    std::filesystem::path cache_dir;
    std::chrono::milliseconds request_timeout{5000};
};

struct ExpansionReport {
    struct Timings {
        std::uint64_t plan_ns = 0;
        std::uint64_t setup_ns = 0;
        std::uint64_t declaration_bridge_ns = 0;
        std::uint64_t request_loop_ns = 0;
        std::uint64_t package_build_ns = 0;
        std::uint64_t package_sdk_prepare_ns = 0;
        std::uint64_t package_compile_ns = 0;
        std::uint64_t package_link_ns = 0;
        std::uint64_t worker_start_ns = 0;
        std::uint64_t protocol_ns = 0;
        std::uint64_t execute_ns = 0;
        std::uint64_t cache_read_ns = 0;
        std::uint64_t cache_key_ns = 0;
        std::uint64_t cache_write_ns = 0;
        std::uint64_t collect_ns = 0;
        std::uint64_t validate_ns = 0;
        std::uint64_t hygiene_ns = 0;
        std::uint64_t merge_ns = 0;
    };

    struct Definition {
        std::string name;
        std::string identity;
        std::string module_path;
        std::string accepted_kind;
        std::string documentation;
        protocol::SourceRange location;
        std::optional<protocol::ClassDecl> attribute_schema;
    };

    std::size_t invocations = 0;
    std::size_t worker_executions = 0;
    std::size_t worker_starts = 0;
    std::size_t worker_cache_hits = 0;
    std::size_t expansion_cache_hits = 0;
    std::size_t generated_nodes = 0;
    std::size_t worker_rss_kb = 0;
    Timings timings;
    std::vector<std::string> worker_identities;
    std::vector<Definition> definitions;
    struct Record {
        std::string macro_name;
        std::string macro_identity;
        std::string target_module;
        std::string target_name;
        protocol::SourceRange invocation;
        protocol::SourceRange definition;
        protocol::SourceRange source_declaration;
        protocol::Expansion expansion;
    };
    std::vector<Record> expansions;
};

ExpansionReport expand_module_macros(ModuleAst& module, const ExpansionOptions& options);

} // namespace dudu::macro
