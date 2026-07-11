#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <vector>

namespace dudu {

void check_function_body_statements(FunctionScope& scope, const std::vector<Stmt>& body,
                                    const TypeRef& return_type_ref);

} // namespace dudu
