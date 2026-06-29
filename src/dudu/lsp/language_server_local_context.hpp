#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>

namespace dudu {

struct Json;

std::optional<std::string> member_completion_target(const Document& doc, const Json* params);
TypeRef local_type_ref_before_cursor(const ModuleAst& module, const std::string& name,
                                     const Json* params = nullptr);
std::map<std::string, TypeRef> local_type_refs_before_cursor(const ModuleAst& module,
                                                             const Json* params);
std::map<std::string, TypeRef> local_type_refs_before_line(const ModuleAst& module,
                                                           int one_based_line);
std::set<std::string> member_candidate_types(const ModuleAst& module, const TypeRef& type);

} // namespace dudu
