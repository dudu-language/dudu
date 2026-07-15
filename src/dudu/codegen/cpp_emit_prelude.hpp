#pragma once

#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace dudu {

std::vector<std::string> namespace_aliases(const ModuleAst& module);
void emit_generated_banner(std::ostringstream& out);
void emit_native_includes(std::ostringstream& out, const ModuleAst& module);
void emit_prelude(std::ostringstream& out, const ModuleAst& module, bool include_native_imports);

} // namespace dudu
