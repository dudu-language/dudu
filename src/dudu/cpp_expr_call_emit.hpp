#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_options.hpp"
#include "dudu/sema_context.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

bool is_builtin_template_constructor(std::string_view name);
std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const std::map<std::string, std::string>& locals,
                              const Symbols* symbols = nullptr);
std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const std::map<std::string, std::string>& locals,
                              const Symbols* symbols, const CppEmitOptions& options);
bool is_pointer_receiver_expr(const Expr& expr, const std::map<std::string, std::string>& locals);
std::string lower_enum_variant_constructor(const EnumDecl& en, const EnumValueDecl& value,
                                           const std::vector<Expr>& args,
                                           const std::vector<std::string>& aliases,
                                           const std::map<std::string, std::string>& locals,
                                           const Symbols* symbols);
std::string lower_enum_variant_constructor(const EnumDecl& en, const EnumValueDecl& value,
                                           const std::vector<Expr>& args,
                                           const std::vector<std::string>& aliases,
                                           const std::map<std::string, std::string>& locals,
                                           const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols);
std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols, const CppEmitOptions& options);
std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const std::map<std::string, std::string>& locals,
                                 const Symbols* symbols = nullptr);
std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const std::map<std::string, std::string>& locals,
                                 const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string> lower_pointer_cast_expr(const Expr& expr,
                                                   const std::vector<std::string>& aliases,
                                                   const std::map<std::string, std::string>& locals,
                                                   const Symbols* symbols = nullptr);
std::optional<std::string> lower_pointer_cast_expr(const Expr& expr,
                                                   const std::vector<std::string>& aliases,
                                                   const std::map<std::string, std::string>& locals,
                                                   const Symbols* symbols,
                                                   const CppEmitOptions& options);
std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols);
std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols, const CppEmitOptions& options);

} // namespace dudu
