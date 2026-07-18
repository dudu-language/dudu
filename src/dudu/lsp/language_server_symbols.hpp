#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::vector<Symbol> symbols_for_module(const ModuleAst& module, bool include_native = true);
std::optional<Symbol> exact_symbol_match(const std::vector<Symbol>& symbols,
                                         const std::string& query);
std::optional<Symbol> native_namespace_segment_symbol(const std::vector<Symbol>& symbols,
                                                      const std::optional<std::string>& selected,
                                                      const std::string& query);
bool is_constructor_method_name(const std::string& name);
std::string function_detail(const FunctionDecl& fn);
std::string constructor_detail(const ClassDecl& klass);
std::string constructor_doc_comment(const ClassDecl& klass);
std::string native_import_provenance(const ModuleAst& module, std::string_view name);
std::vector<Symbol::Parameter> function_symbol_parameters(const FunctionDecl& function);
std::vector<Symbol::Parameter> constructor_symbol_parameters(const ClassDecl& klass);
Symbol method_symbol(const FunctionDecl& method, bool native);
std::vector<Symbol::Parameter>
native_function_symbol_parameters(const NativeFunctionDecl& function);
std::string native_macro_detail(const NativeMacroDecl& macro);
std::string native_function_detail(const NativeFunctionDecl& fn);
std::optional<std::string> native_identity_key(const NativeSymbolId& identity);
std::optional<std::string> native_class_member_identity_key(const ClassDecl& klass,
                                                            const std::string& member);

} // namespace dudu
