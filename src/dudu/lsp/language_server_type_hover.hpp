#pragma once

#include "dudu/core/ast.hpp"

#include <optional>
#include <string>

namespace dudu {

std::optional<std::string> primitive_type_hover_json(const std::string& word);
std::optional<std::string> native_alias_hover_json(const std::string& word,
                                                   const ModuleAst& module);
std::string class_hover_json(const ModuleAst& module, const ClassDecl& klass, bool native);

} // namespace dudu
