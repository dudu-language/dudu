#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

std::string indent(int depth);
std::string cpp_string_literal(std::string text);
std::string if_keyword_for_condition(const Expr& condition);

} // namespace dudu
