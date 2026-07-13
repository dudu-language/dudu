#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/project/project_config.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace dudu::macro {

struct ExpansionOptions {
    ProjectConfig project;
    std::filesystem::path cache_dir;
    std::chrono::milliseconds request_timeout{5000};
};

struct ExpansionReport {
    std::size_t invocations = 0;
    std::size_t worker_cache_hits = 0;
    std::size_t expansion_cache_hits = 0;
    std::vector<std::string> worker_identities;
};

ExpansionReport expand_module_macros(ModuleAst& module, const ExpansionOptions& options);

} // namespace dudu::macro
