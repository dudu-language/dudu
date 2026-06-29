#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct ModuleAst;
struct ReferenceLocation;

std::vector<ReferenceLocation> references_in(const ModuleAst& module, const Document& doc,
                                             const std::string& query);
std::optional<std::vector<ReferenceLocation>>
references_in_local_scope(const ModuleAst& module, const Document& doc, const std::string& query,
                          int one_based_line);

} // namespace dudu
