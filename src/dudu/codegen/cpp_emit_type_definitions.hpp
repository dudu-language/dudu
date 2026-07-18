#pragma once

#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

void emit_class_and_payload_enum_definitions(std::ostringstream& out, const ModuleAst& module,
                                             const std::vector<std::string>& aliases,
                                             const std::map<std::string, TypeRef>& function_returns,
                                             const Symbols& symbols, bool header_only = false,
                                             const CppEmitOptions& options = {});

} // namespace dudu
