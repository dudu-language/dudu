#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"

#include <optional>
#include <string>

namespace dudu {

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                        const Expr& right_expr, const std::string& right);
bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                            const Expr& right_expr, const std::string& right);
bool is_integer_type(std::string type);
std::optional<FunctionSignature>
dudu_operator_signature(const Symbols& symbols, const std::string& op, const std::string& left);
std::optional<FunctionSignature>
binary_operator_signature(const Symbols& symbols, const std::string& op, const std::string& left,
                          const Expr& right_expr, const std::string& right);

} // namespace dudu
