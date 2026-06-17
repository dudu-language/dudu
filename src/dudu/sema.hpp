#pragma once

#include "dudu/ast.hpp"

namespace dudu {

struct SemanticOptions {
    bool check_bodies = false;
};

void analyze_module(const ModuleAst& module, SemanticOptions options = {});
void analyze_module_tree(const ModuleAst& module, SemanticOptions options = {});
void reject_direct_backend_module_conflicts(const ModuleAst& module);

} // namespace dudu
