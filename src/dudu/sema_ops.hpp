#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"

#include <optional>
#include <string>

namespace dudu {

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const TypeRef& left,
                        const Expr& right_expr, const TypeRef& right);
bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const TypeRef& left,
                            const Expr& right_expr, const TypeRef& right);
bool is_integer_type(std::string type);
std::optional<FunctionSignature>
dudu_operator_signature(const Symbols& symbols, const std::string& op, const std::string& left);
std::optional<FunctionSignature>
binary_operator_signature(const Symbols& symbols, const std::string& op, const TypeRef& left,
                          const Expr& right_expr, const TypeRef& right);

} // namespace dudu
