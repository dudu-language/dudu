#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_options.hpp"
#include "dudu/sema_context.hpp"

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {

bool has_expr(const Expr& expr);
std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals,
                               std::string_view separator = ", ", const Symbols* symbols = nullptr);
std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals,
                               std::string_view separator, const Symbols* symbols,
                               const CppEmitOptions& options);
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals,
                       const Symbols* symbols = nullptr);
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals,
                       const std::map<std::string, TypeRef>& local_type_refs,
                       const Symbols* symbols, const CppEmitOptions& options);
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals, const Symbols* symbols,
                       const CppEmitOptions& options);
std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals,
                                const Symbols* symbols = nullptr);
std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals,
                                const std::map<std::string, TypeRef>& local_type_refs,
                                const Symbols* symbols, const CppEmitOptions& options);
std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals,
                                const Symbols* symbols, const CppEmitOptions& options);
std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals = {});
std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals,
                               const CppEmitOptions& options);
std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols);
bool enum_has_payloads(const EnumDecl& en);

} // namespace dudu
