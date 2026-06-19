#pragma once

#include "dudu/ast.hpp"
#include "dudu/token.hpp"

#include <optional>
#include <string_view>

namespace dudu {

bool is_assignment_operator(const Token& token);
bool is_compound_assignment_operator(const Token& token);
std::optional<CompoundAssignOp> compound_assignment_op(std::string_view token_text);

} // namespace dudu
