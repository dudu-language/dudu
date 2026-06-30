#pragma once

#include "dudu/core/ast.hpp"

#include <string>

namespace dudu {

std::string class_hover_json(const ModuleAst& module, const ClassDecl& klass, bool native);

} // namespace dudu
