#pragma once

#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

std::string emitted_enum_method_name(const EnumDecl& en, const FunctionDecl& method,
                                     const CppEmitOptions& options = {});

void emit_enum_method_declarations(std::ostringstream& out, const ModuleAst& module,
                                   const std::vector<std::string>& aliases, bool header_only,
                                   const CppEmitOptions& options = {});

void emit_private_enum_method_declarations(std::ostringstream& out, const ModuleAst& module,
                                           const std::vector<std::string>& aliases,
                                           const CppEmitOptions& options = {});

void emit_enum_method_definitions(std::ostringstream& out, const ModuleAst& module,
                                  const std::vector<std::string>& aliases,
                                  const std::map<std::string, TypeRef>& function_returns,
                                  const Symbols& symbols, bool header_only,
                                  const CppEmitOptions& options = {});

void emit_enum_method_dispatch_overloads(std::ostringstream& out, const ModuleAst& module,
                                         const std::vector<std::string>& aliases,
                                         const CppEmitOptions& options = {});

} // namespace dudu
