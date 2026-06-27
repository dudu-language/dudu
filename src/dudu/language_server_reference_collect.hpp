#pragma once

#include "dudu/language_server_types.hpp"

#include <string>
#include <vector>

namespace dudu {

struct ModuleAst;

std::vector<ReferenceLocation> references_in(const ModuleAst& module, const Document& doc,
                                             const std::string& query);
std::vector<ReferenceLocation> references_in(const Document& doc, const std::string& query);

} // namespace dudu
