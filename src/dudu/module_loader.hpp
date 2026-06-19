#pragma once

#include "dudu/ast.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace dudu {

ModuleAst load_source_tree(const std::filesystem::path& entry);
ModuleAst load_source_tree(const std::filesystem::path& entry, std::string_view entry_source);
std::vector<std::filesystem::path> source_tree_files(const std::filesystem::path& entry);

} // namespace dudu
