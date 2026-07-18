#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::string type_ref_binding_name(const TypeRef& type);
std::optional<Symbol> native_symbol_for_identity(const std::vector<Symbol>& symbols,
                                                 std::string_view identity);
const ClassDecl* native_class_for_identity(const ModuleAst& module, std::string_view identity);
std::optional<Symbol> native_type_symbol_for_type_ref(const ModuleAst& module, const TypeRef& type);

} // namespace dudu
