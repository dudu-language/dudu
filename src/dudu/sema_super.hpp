#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_scope.hpp"

#include <string>

namespace dudu {

bool is_super_call(const std::string& callee);
bool is_super_init_stmt(const Stmt& stmt);
TypeRef infer_super_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                  const std::string& callee, const SourceLocation* location);

} // namespace dudu
