#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_scope.hpp"

namespace dudu {

TypeRef assignment_target_type_ref(FunctionScope& scope, const Stmt& stmt);

} // namespace dudu
