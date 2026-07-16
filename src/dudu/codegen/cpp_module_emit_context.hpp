#pragma once

#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"

#include <map>
#include <string>

namespace dudu {

using CppModuleMap = std::map<std::string, const ModuleAst*>;

bool preserve_public_abi_names(const ModuleAst& module);
std::string resolved_module_path_for_import(const ModuleAst& unit, const ImportDecl& import);
CppEmitOptions make_cpp_module_emit_options(const ModuleAst& unit, const CppModuleMap& modules,
                                            bool test_source = false, bool public_abi = false,
                                            bool include_macro_host_modules = false);

} // namespace dudu
