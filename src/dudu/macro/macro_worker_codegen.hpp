#pragma once

#include "dudu/macro/macro_registry.hpp"

#include <set>
#include <string>

namespace dudu::macro {

struct WorkerSourceOptions {
    std::string package;
    std::string binary_identity;
    std::string project_root;
    std::vector<std::string> capabilities;
    std::set<std::string> non_cacheable_macros;
};

std::string generate_worker_source(const Plan& plan, const WorkerSourceOptions& options);

} // namespace dudu::macro
