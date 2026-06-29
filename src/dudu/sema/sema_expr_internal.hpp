#pragma once

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/project/build_flags.hpp"
#include "dudu/core/control_flow.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/escapes.hpp"
#include "dudu/core/naming.hpp"
#include "dudu/sema/sema_alloc.hpp"
#include "dudu/sema/sema_bindings.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_constexpr.hpp"
#include "dudu/sema/sema_constructors.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_enum.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_inheritance.hpp"
#include "dudu/sema/sema_match.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_native.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/sema_scan.hpp"
#include "dudu/sema/sema_super.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/unsupported.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {

[[noreturn]] void sema_expr_fail(const SourceLocation& location, const std::string& message);

bool is_cpp_escape_member_path_string(const std::string& path);

TypeRef infer_cpp_escape_expr_ref(const FunctionScope& scope, std::string expr,
                                  const SourceLocation* location = nullptr);
TypeRef infer_expr_type_ast(const FunctionScope& scope, const Expr& expr,
                            const SourceLocation* location);
void check_expr_ast(const FunctionScope& scope, const Expr& expr, const SourceLocation* location);
std::optional<TypeRef> direct_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                            const SourceLocation* location);
std::optional<TypeRef> direct_template_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                     const SourceLocation* location);
std::optional<TypeRef> direct_member_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const std::string& callee,
                                                   const SourceLocation* location);
std::optional<TypeRef> direct_member_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location);
std::optional<TypeRef> direct_template_member_call_type_ref(const FunctionScope& scope,
                                                            const Expr& expr,
                                                            const std::string& callee,
                                                            const SourceLocation* location);
std::optional<TypeRef> member_expr_direct_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location);
std::optional<TypeRef> unary_expr_type_ref(const FunctionScope& scope, const Expr& expr,
                                           const SourceLocation* location);
std::optional<TypeRef> binary_expr_type_ref(const FunctionScope& scope, const Expr& expr,
                                            const SourceLocation* location);
void check_call_args_ast(const FunctionScope& scope, const std::string& callee,
                         const FunctionSignature& signature, const std::vector<Expr>& args,
                         const SourceLocation* location);
std::optional<FunctionSignature>
matching_signature_ast(const FunctionScope& scope, const std::vector<FunctionSignature>& options,
                       const std::vector<Expr>& args);
bool can_assign_ast(const FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                    const TypeRef& got);
bool is_builtin_call(const std::string& callee);
bool is_local_member_call(const FunctionScope& scope, const std::string& callee);
void reject_abstract_construction(const Symbols& symbols, const TypeRef& type,
                                  const SourceLocation* location);
bool is_comparison_op(std::string_view op);
bool is_arithmetic_op(std::string_view op);
std::optional<std::string> contextual_numeric_binary_type(const FunctionScope& scope,
                                                          const Expr& left_expr,
                                                          const std::string& left,
                                                          const Expr& right_expr,
                                                          const std::string& right);
bool parse_local_function_type(const FunctionScope& scope, const std::string& name,
                               FunctionSignature& out);
void check_enum_variant_args_ast(const FunctionScope& scope, const EnumDecl& en,
                                 const EnumValueDecl& value, const std::vector<Expr>& args,
                                 const SourceLocation* location);
std::string template_call_callee(const FunctionScope& scope, const Expr& expr,
                                 const SourceLocation* location);
bool is_offsetof_field_expr(const Expr& expr);
std::optional<FunctionSignature> explicit_generic_function_signature_ast(
    const FunctionScope& scope, const Expr& expr, const std::string& callee_base,
    const std::string& emitted_callee, const SourceLocation* location);
} // namespace dudu
