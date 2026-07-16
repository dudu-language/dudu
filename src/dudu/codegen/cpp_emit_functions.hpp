#pragma once

#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

std::map<std::string, TypeRef> cpp_function_return_types(const ModuleAst& module);

bool cpp_function_visible_in_header(const FunctionDecl& fn, const CppEmitOptions& options = {});

void emit_cpp_function_declarations(std::ostringstream& out, const ModuleAst& module,
                                    const std::vector<std::string>& aliases, bool header_only,
                                    bool test_source, const CppEmitOptions& options);

void emit_cpp_early_functions(std::ostringstream& out, const ModuleAst& module,
                              const std::vector<std::string>& aliases,
                              const std::map<std::string, TypeRef>& function_returns,
                              const Symbols& symbols, bool header_only, bool test_source,
                              const CppEmitOptions& options);

void emit_cpp_header_generic_function_bodies(std::ostringstream& out, const ModuleAst& module,
                                             const std::vector<std::string>& aliases,
                                             const std::map<std::string, TypeRef>& function_returns,
                                             const Symbols& symbols, const CppEmitOptions& options);

void emit_cpp_remaining_function_bodies(std::ostringstream& out, const ModuleAst& module,
                                        const std::vector<std::string>& aliases,
                                        const std::map<std::string, TypeRef>& function_returns,
                                        const Symbols& symbols, bool test_source,
                                        const CppEmitOptions& options);

void emit_cpp_module_function_bodies(std::ostringstream& out, const ModuleAst& module,
                                     const std::vector<std::string>& aliases,
                                     const std::map<std::string, TypeRef>& function_returns,
                                     const Symbols& symbols, const CppEmitOptions& options);

void emit_c_function_declarations(std::ostringstream& out, const ModuleAst& module);

} // namespace dudu
