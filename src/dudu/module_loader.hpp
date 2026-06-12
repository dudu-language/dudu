#pragma once

#include "dudu/ast.hpp"

#include <filesystem>

namespace dudu {

ModuleAst load_source_tree(const std::filesystem::path& entry);

} // namespace dudu
