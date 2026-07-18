#pragma once

#include "dudu/core/ast.hpp"

#include <optional>
#include <string>

namespace dudu {

struct Symbols;

std::optional<std::string> primitive_type_hover_json(const std::string& word);
std::optional<std::string> native_alias_hover_json(const std::string& word,
                                                   const ModuleAst& module);
std::string class_hover_json(const ModuleAst& module, const ClassDecl& klass, bool native);
std::string enum_hover_json(const EnumDecl& en);
std::string type_name_hover_markdown(const Symbols& symbols, const std::string& name);
SourceLocation type_name_definition_location(const Symbols& symbols, const std::string& name);

} // namespace dudu
