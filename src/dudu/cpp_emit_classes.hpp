#pragma once

#include "dudu/ast.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

void emit_classes(std::ostringstream& out, const ModuleAst& module,
                  const std::vector<std::string>& aliases,
                  const std::map<std::string, std::string>& function_returns,
                  bool header_only = false);

} // namespace dudu
