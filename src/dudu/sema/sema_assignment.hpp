#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_scope.hpp"

namespace dudu {

TypeRef assignment_target_type_ref(FunctionScope& scope, const Stmt& stmt);
TypeRef compound_assignment_target_type_ref(FunctionScope& scope, const Stmt& stmt);

} // namespace dudu
