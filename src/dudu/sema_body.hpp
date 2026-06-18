#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"

namespace dudu {

void check_bodies(const ModuleAst& module, const Symbols& symbols);

} // namespace dudu
