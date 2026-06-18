#pragma once

#include "dudu/ast.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/sema_scope.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

[[noreturn]] void sema_fail(const SourceLocation& location, const std::string& message);
bool sema_has_expr(const Expr& expr);
bool missing_expr(const Expr& expr);
const SourceLocation& node_location(const SourceLocation& fallback, const Expr& expr);
const SourceLocation& node_location(const SourceLocation& fallback, const TypeRef& type);
void bind_local(FunctionScope& scope, const std::string& name, const std::string& type,
                const TypeRef& type_ref = {});
std::vector<Expr> index_arg_exprs(const Expr& index_expr);
std::optional<ExprPath> scoped_expr_path_from_expr(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location);
std::optional<ExprPath> scoped_call_callee_path(const FunctionScope& scope, const Expr& expr,
                                                const SourceLocation* location);
std::string scoped_call_callee_text(const FunctionScope& scope, const Expr& expr,
                                    const SourceLocation* location);

} // namespace dudu
