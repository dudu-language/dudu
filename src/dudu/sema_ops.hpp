#pragma once

#include "dudu/sema_context.hpp"

#include <string>

namespace dudu {

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                        const std::string& right_expr, const std::string& right);
bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                            const std::string& right_expr, const std::string& right);

} // namespace dudu
