#pragma once

#include "dudu/core/ast.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

ModuleAst load_source_tree(const std::filesystem::path& entry);
ModuleAst load_source_tree(const std::filesystem::path& entry, std::string_view entry_source);
ModuleAst load_source_tree(const std::filesystem::path& entry,
                           const std::map<std::filesystem::path, std::string>& source_overrides);
std::vector<std::filesystem::path> source_tree_files(const std::filesystem::path& entry);

} // namespace dudu
