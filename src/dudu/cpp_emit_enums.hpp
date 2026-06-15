#pragma once

#include "dudu/ast.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace dudu {

void emit_enum_forward_declarations(std::ostringstream& out, const ModuleAst& module);
void emit_enums(std::ostringstream& out, const ModuleAst& module,
                const std::vector<std::string>& aliases);

} // namespace dudu
