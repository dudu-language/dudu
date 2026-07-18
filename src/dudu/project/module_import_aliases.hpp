#pragma once

#include "dudu/core/ast.hpp"

namespace dudu {

void add_qualified_module_symbols(ModuleAst& module, const ModuleAst& dependency,
                                  const ImportDecl& import);
void add_selective_module_symbol(ModuleAst& module, const ModuleAst& dependency,
                                 const ImportDecl& import);
bool selective_module_symbol_already_projected(const ModuleAst& module, const ModuleAst& dependency,
                                               const ImportDecl& import);
void refresh_projected_module_symbols(ModuleAst& module);

} // namespace dudu
