#pragma once

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/build_flags.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/escapes.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_body.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_constexpr.hpp"
#include "dudu/sema_constructors.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_enum.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_generics.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/sema_match.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_native.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/sema_super.hpp"
#include "dudu/type_compat.hpp"
#include "dudu/unsupported.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {

[[noreturn]] void sema_expr_fail(const SourceLocation& location, const std::string& message);

std::string member_path_type_from_string(const Symbols& symbols,
                                         const std::map<std::string, std::string>& locals,
                                         const SourceLocation* location, const std::string& path,
                                         std::string unknown_local_prefix);
bool is_member_path(const std::string& path);

std::string infer_cpp_escape_expr(const FunctionScope& scope, std::string expr,
                                  const SourceLocation* location = nullptr);
std::string infer_expr_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* location = nullptr);
TypeRef infer_expr_type_ast(const FunctionScope& scope, const Expr& expr,
                            const SourceLocation* location = nullptr);
void check_call_args_ast(const FunctionScope& scope, const std::string& callee,
                         const FunctionSignature& signature, const std::vector<Expr>& args,
                         const SourceLocation* location);
std::optional<FunctionSignature>
matching_signature_ast(const FunctionScope& scope, const std::vector<FunctionSignature>& options,
                       const std::vector<Expr>& args);
std::vector<Expr> call_arg_exprs(std::string expr, size_t open, SourceLocation location);
std::vector<Expr> parse_exprs(const std::vector<std::string>& exprs, SourceLocation location);
bool can_assign_ast(const FunctionScope& scope, const std::string& expected, const Expr& expr,
                    const std::string& got);
bool is_builtin_call(const std::string& callee);
bool is_local_member_call(const FunctionScope& scope, const std::string& callee);
void reject_abstract_construction(const Symbols& symbols, const std::string& type,
                                  const SourceLocation* location);
bool is_comparison_op(const std::string& op);
bool parse_local_function_type(const FunctionScope& scope, const std::string& name,
                               const std::string& type, FunctionSignature& out);
void check_enum_variant_args_ast(const FunctionScope& scope, const EnumDecl& en,
                                 const EnumValueDecl& value, const std::vector<Expr>& args,
                                 const SourceLocation* location);
std::string template_call_callee(const FunctionScope& scope, const Expr& expr,
                                 const SourceLocation* location);
bool is_offsetof_field_expr(const Expr& expr);
std::string infer_template_call_ast(const FunctionScope& scope, const Expr& expr,
                                    const SourceLocation* location);
std::string infer_constructor_call_ast(const FunctionScope& scope, const Expr& expr,
                                       const std::string& callee, const SourceLocation* location);
std::string infer_builtin_call_ast(const FunctionScope& scope, const Expr& expr,
                                   const std::string& callee, const SourceLocation* location);
std::optional<std::string> infer_pointer_cast_call_ast(const FunctionScope& scope, const Expr& expr,
                                                       const std::string& callee,
                                                       const SourceLocation* location);
std::string infer_call_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* location);

} // namespace dudu
