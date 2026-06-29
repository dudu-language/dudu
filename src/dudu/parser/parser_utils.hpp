#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/token.hpp"

#include <string_view>
#include <vector>

namespace dudu {

bool is_all_caps_identifier(const Token& token);
void validate_import_bindings(const std::vector<ImportDecl>& imports);

} // namespace dudu
