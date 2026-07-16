#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"

namespace dudu {

void add_imported_native_type_support(Symbols& symbols, const ModuleAst& module,
                                      const ModuleAst& module_tree);

} // namespace dudu
