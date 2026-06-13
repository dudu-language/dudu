#pragma once

#include "dudu/sema_context.hpp"

#include <optional>
#include <string>

namespace dudu {

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                        const std::string& right_expr, const std::string& right);
bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                            const std::string& right_expr, const std::string& right);
std::optional<FunctionSignature> dudu_operator_signature(const Symbols& symbols,
                                                         const std::string& op,
                                                         const std::string& left);

} // namespace dudu
