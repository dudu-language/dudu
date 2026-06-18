#pragma once

#include "dudu/ast.hpp"
#include "dudu/language_server_types.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>

namespace dudu {

struct Json;

std::optional<std::string> member_completion_target(const Document& doc, const Json* params);
std::string local_type_before_cursor(const Document& doc, const std::string& name,
                                     const Json* params = nullptr);
std::map<std::string, TypeRef> local_type_refs_before_cursor(const Document& doc,
                                                             const Json* params);
std::set<std::string> member_candidate_types(const ModuleAst& module, const std::string& type);

} // namespace dudu
