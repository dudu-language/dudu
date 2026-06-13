#pragma once

#include "dudu/ast.hpp"

#include <filesystem>
#include <vector>

namespace dudu {

ModuleAst load_source_tree(const std::filesystem::path& entry);
std::vector<std::filesystem::path> source_tree_files(const std::filesystem::path& entry);

} // namespace dudu
