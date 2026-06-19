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
const SourceLocation& diagnostic_location(const SourceLocation& context, const Expr& expr);
const SourceLocation& diagnostic_location(const SourceLocation& context, const TypeRef& type);
void bind_local(FunctionScope& scope, const std::string& name, const TypeRef& type_ref);
Symbols with_generic_params(Symbols symbols, const std::vector<std::string>& params);
std::vector<Expr> index_arg_exprs(const Expr& index_expr);
struct ScopedCallee {
    std::optional<ExprPath> path;
    std::string key;
};

std::optional<ExprPath> scoped_expr_path_from_expr(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location);
std::optional<ExprPath> scoped_call_callee_path(const FunctionScope& scope, const Expr& expr,
                                                const SourceLocation* location);
ScopedCallee scoped_call_callee(const FunctionScope& scope, const Expr& expr,
                                const SourceLocation* location);

} // namespace dudu
