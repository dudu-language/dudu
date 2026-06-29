#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace dudu {

bool binary_rhs_allowed(const Symbols& symbols, std::string_view op, const TypeRef& left,
                        const Expr& right_expr, const TypeRef& right);
bool comparison_rhs_allowed(const Symbols& symbols, std::string_view op, const TypeRef& left,
                            const Expr& right_expr, const TypeRef& right);
std::optional<FunctionSignature> dudu_operator_signature(const Symbols& symbols,
                                                         std::string_view op, const TypeRef& left);
std::optional<FunctionSignature> binary_operator_signature(const Symbols& symbols,
                                                           std::string_view op, const TypeRef& left,
                                                           const Expr& right_expr,
                                                           const TypeRef& right);

} // namespace dudu
