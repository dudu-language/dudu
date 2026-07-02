#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

bool binary_rhs_allowed(const Symbols& symbols, std::string_view op, const TypeRef& left,
                        const Expr& right_expr, const TypeRef& right);
bool comparison_rhs_allowed(const Symbols& symbols, std::string_view op, const TypeRef& left,
                            const Expr& right_expr, const TypeRef& right);
std::optional<FunctionSignature> dudu_operator_signature(const Symbols& symbols,
                                                         std::string_view op, const TypeRef& left);
std::optional<FunctionSignature>
dudu_operator_signature_for_args(const Symbols& symbols, std::string_view op, const TypeRef& left,
                                 const std::vector<Expr>& args,
                                 const std::vector<TypeRef>& arg_types);
std::optional<FunctionSignature>
dudu_operator_signature_for_arg_types(const Symbols& symbols, std::string_view op,
                                      const TypeRef& left, const std::vector<TypeRef>& arg_types);
std::optional<std::string>
dudu_operator_method_name_for_arg_types(const Symbols& symbols, std::string_view op,
                                        const TypeRef& left, const std::vector<TypeRef>& arg_types);
std::optional<std::string>
dudu_operator_method_name_for_args(const Symbols& symbols, std::string_view op, const TypeRef& left,
                                   const std::vector<Expr>& args,
                                   const std::vector<TypeRef>& arg_types);
std::string dudu_operator_no_match_message_for_args(
    const Symbols& symbols, std::string_view op, const TypeRef& left,
    const std::vector<Expr>& args, const std::vector<TypeRef>& arg_types,
    std::string_view action, std::string_view label);
std::optional<FunctionSignature> binary_operator_signature(const Symbols& symbols,
                                                           std::string_view op, const TypeRef& left,
                                                           const Expr& right_expr,
                                                           const TypeRef& right);

} // namespace dudu
