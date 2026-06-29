#pragma once

#include "dudu/core/ast.hpp"

#include <filesystem>
#include <string>

namespace dudu {

class ProjectIndex;

std::string semantic_tokens_json(const ModuleAst& module);
std::string semantic_tokens_json(const ModuleAst& module, const ModuleAst& native_symbols);
std::string semantic_tokens_json(const ProjectIndex& index, const std::filesystem::path& path,
                                 const ProjectIndex& native_index);

} // namespace dudu
