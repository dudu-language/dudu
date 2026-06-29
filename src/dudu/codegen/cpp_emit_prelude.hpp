#pragma once

#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace dudu {

std::vector<std::string> namespace_aliases(const ModuleAst& module);
void emit_includes(std::ostringstream& out, const ModuleAst& module);
void emit_result_prelude(std::ostringstream& out, const ModuleAst& module);

} // namespace dudu
