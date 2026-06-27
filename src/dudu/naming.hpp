#pragma once

#include "dudu/ast.hpp"

namespace dudu {

bool is_dudu_all_caps(const std::string& name);
bool is_dudu_snake_case(const std::string& name);
bool is_constructor_method(const FunctionDecl& method);
bool is_destructor_method(const FunctionDecl& method);
void check_naming(const ModuleAst& module);

} // namespace dudu
