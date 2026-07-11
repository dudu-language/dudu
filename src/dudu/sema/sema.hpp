#pragma once

#include "dudu/core/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

struct SemanticOptions {
    bool check_bodies = false;
};

void analyze_module(const ModuleAst& module, SemanticOptions options = {});
std::vector<CompileError> analyze_module_collecting(const ModuleAst& module,
                                                    SemanticOptions options = {});
void analyze_module_tree(const ModuleAst& module, SemanticOptions options = {});
std::vector<CompileError> analyze_module_tree_collecting(const ModuleAst& module,
                                                         SemanticOptions options = {});
void analyze_module_tree(const ModuleAst& module, const std::vector<std::string>& module_paths,
                         SemanticOptions options = {});
void reject_merged_output_module_conflicts(const ModuleAst& module);

} // namespace dudu
