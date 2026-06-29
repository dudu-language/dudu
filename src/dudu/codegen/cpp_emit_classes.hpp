#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

void emit_classes(std::ostringstream& out, const ModuleAst& module,
                  const std::vector<std::string>& aliases,
                  const std::map<std::string, TypeRef>& function_returns,
                  const Symbols& symbols, bool header_only = false,
                  const CppEmitOptions& options = {});

} // namespace dudu
