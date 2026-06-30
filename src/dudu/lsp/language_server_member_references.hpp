#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct ExprPath;
struct Json;
struct ModuleAst;

std::optional<std::string> member_declaration_reference_query_at(const Document& doc,
                                                                 const Json* params,
                                                                 const ModuleAst* module);
std::optional<std::string> enum_value_declaration_reference_query_at(const Document& doc,
                                                                     const Json* params,
                                                                     const ModuleAst* module);
std::optional<std::string> member_use_reference_query_at(const ModuleAst& module,
                                                         const Document& doc, const ExprPath& path,
                                                         const Json* params);

} // namespace dudu
