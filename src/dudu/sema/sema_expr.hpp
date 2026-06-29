#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_scope.hpp"

namespace dudu {

TypeRef infer_expr_type_ast(const FunctionScope& scope, const Expr& expr,
                            const SourceLocation* location = nullptr);
void check_expr_ast(const FunctionScope& scope, const Expr& expr,
                    const SourceLocation* location = nullptr);

} // namespace dudu
