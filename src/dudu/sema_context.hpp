#pragma once

#include "dudu/ast.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace dudu {

struct Symbols {
    std::set<std::string> types;
    std::map<std::string, std::string> aliases;
    std::map<std::string, std::string> functions;
    std::map<std::string, const ClassDecl*> classes;
};

std::string trim(std::string text);
std::string base_type(std::string type);
bool known_type(const Symbols& symbols, const std::string& type);
std::string resolve_alias(const Symbols& symbols, std::string type);
std::vector<std::string> split_top_level(std::string text);
size_t find_top_level_char(const std::string& text, char wanted);
std::vector<std::string> tuple_types(const Symbols& symbols, std::string type);
Symbols collect_symbols(const ModuleAst& module);
void check_declarations(const ModuleAst& module, const Symbols& symbols);

} // namespace dudu
