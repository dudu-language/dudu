#pragma once

#include "dudu/core/ast.hpp"

#include <string_view>

namespace dudu {

std::string normalize_docstring_text(std::string_view text);
void attach_leading_doc_comments(ModuleAst& module, std::string_view source);

} // namespace dudu
