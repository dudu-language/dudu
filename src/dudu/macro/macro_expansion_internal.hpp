#pragma once

#include "dudu/macro/macro_protocol_generated.hpp"
#include "dudu/macro/macro_registry.hpp"

#include <string>
#include <vector>

namespace dudu::macro {

struct CollectedExpansion {
    std::string macro_name;
    std::string macro_identity;
    std::string target_module;
    std::string target_name;
    TargetKind target_kind = TargetKind::Any;
    SourceRange invocation;
    SourceRange definition;
    SourceRange source_declaration;
    protocol::Expansion expansion;
};

void merge_expansions(ModuleAst& module, const Plan& plan,
                      const std::vector<CollectedExpansion>& expansions);

} // namespace dudu::macro
