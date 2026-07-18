#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <optional>
#include <string>

namespace dudu {

void bind_lsp_local(FunctionScope& scope, const std::string& name, TypeRef type);
TypeRef infer_lsp_expr_type(FunctionScope& scope, const Expr& expr);
TypeRef try_infer_lsp_expr_type(FunctionScope& scope, const Expr& expr);
TypeRef lsp_variable_type(const Stmt& stmt);
std::optional<TypeRef> infer_lsp_loop_binding_type(FunctionScope& scope, const Stmt& stmt);
void bind_lsp_tuple_names(FunctionScope& scope, const Stmt& stmt);
bool try_bind_lsp_tuple_names(FunctionScope& scope, const Stmt& stmt);
void bind_lsp_statement(FunctionScope& scope, const Stmt& stmt);
void bind_lsp_match_case(FunctionScope& scope, const TypeRef& subject_type,
                         const Stmt& case_statement);
Symbols symbols_for_lsp_function(Symbols symbols, const FunctionDecl& fn);
void bind_lsp_function_params(FunctionScope& scope, const FunctionDecl& fn);

} // namespace dudu
