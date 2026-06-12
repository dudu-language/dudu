#pragma once

#include "dudu/ast.hpp"

namespace dudu {

bool is_dudu_all_caps(const std::string& name);
bool is_dudu_snake_case(const std::string& name);
void check_naming(const ModuleAst& module);

} // namespace dudu
