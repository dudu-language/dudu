#pragma once

#include "dudu/ast.hpp"
#include "dudu/language_server_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

std::vector<Symbol> symbols_for_module(const ModuleAst& module, bool include_native = true);
std::vector<Symbol> symbols_for_document(const ModuleAst& module, const Document& doc,
                                         bool include_native = true);
std::vector<Symbol> symbols_for_document(const Document& doc, bool include_native = true);
std::vector<Symbol> visible_symbols_for_document(const ModuleAst& module, const Document& doc,
                                                 bool include_native = true);
std::optional<Symbol> exact_symbol_match(const std::vector<Symbol>& symbols,
                                         const std::string& query);
std::optional<Symbol> unambiguous_suffix_symbol_match(const std::vector<Symbol>& symbols,
                                                      const std::string& query);
bool is_constructor_method_name(const std::string& name);
std::string function_detail(const FunctionDecl& fn);
std::string native_macro_detail(const NativeMacroDecl& macro);
std::string native_function_detail(const NativeFunctionDecl& fn);

} // namespace dudu
