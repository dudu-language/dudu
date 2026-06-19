#pragma once

#include "dudu/ast.hpp"
#include "dudu/language_server_types.hpp"

#include <string>
#include <vector>

namespace dudu {

std::vector<Symbol> symbols_for_module(const ModuleAst& module, bool include_native = true);
std::vector<Symbol> symbols_for_document(const Document& doc, bool include_native = true);
bool is_constructor_method_name(const std::string& name);
std::string function_detail(const FunctionDecl& fn);
std::string native_macro_detail(const NativeMacroDecl& macro);
std::string native_function_detail(const NativeFunctionDecl& fn);

} // namespace dudu
