#pragma once

#include "dudu/codegen/cpp_emit_context.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {

bool has_expr(const Expr& expr);
std::string lower_cpp_escape_expr(std::string expr, const std::vector<std::string>& aliases,
                                  const std::map<std::string, TypeRef>& local_type_refs);
std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const CppLocalContext& locals, std::string_view separator = ", ",
                               const Symbols* symbols = nullptr);
std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const CppLocalContext& locals, std::string_view separator,
                               const Symbols* symbols, const CppEmitOptions& options);
std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const CppLocalContext& locals,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               std::string_view separator, const Symbols* symbols,
                               const CppEmitOptions& options);
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals, const Symbols* symbols = nullptr);
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals,
                       const std::map<std::string, TypeRef>& local_type_refs,
                       const Symbols* symbols, const CppEmitOptions& options);
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals, const Symbols* symbols,
                       const CppEmitOptions& options);
std::string emitted_member_name_for_expr(const Expr& member,
                                         const std::map<std::string, TypeRef>& local_type_refs,
                                         const Symbols* symbols, const CppEmitOptions& options);
std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const CppLocalContext& locals, const Symbols* symbols = nullptr);
std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const CppLocalContext& locals,
                                const std::map<std::string, TypeRef>& local_type_refs,
                                const Symbols* symbols, const CppEmitOptions& options);
std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const CppLocalContext& locals, const Symbols* symbols,
                                const CppEmitOptions& options);
std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const CppLocalContext& locals = {});
std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const CppLocalContext& locals, const CppEmitOptions& options);

} // namespace dudu
