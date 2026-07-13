#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace dudu {

void emit_enum_forward_declarations(std::ostringstream& out, const ModuleAst& module,
                                    const CppEmitOptions& options = {});
void emit_value_enums(std::ostringstream& out, const ModuleAst& module,
                      const std::vector<std::string>& aliases,
                      const CppEmitOptions& options = {});
void emit_payload_enums(std::ostringstream& out, const ModuleAst& module,
                        const std::vector<std::string>& aliases,
                        const CppEmitOptions& options = {});
void emit_enums(std::ostringstream& out, const ModuleAst& module,
                const std::vector<std::string>& aliases, const CppEmitOptions& options = {});

} // namespace dudu
