#pragma once

#include "dudu/ast.hpp"

namespace dudu {

void add_qualified_module_symbols(ModuleAst& module, const ModuleAst& dependency,
                                  const ImportDecl& import);
void add_selective_module_symbol(ModuleAst& module, const ModuleAst& dependency,
                                 const ImportDecl& import);

} // namespace dudu

