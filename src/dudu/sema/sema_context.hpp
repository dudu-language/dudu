#pragma once

#include "dudu/core/ast.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dudu {

struct FunctionSignature {
    std::vector<std::string> template_params;
    std::vector<TypeRef> param_type_refs;
    TypeRef return_type_ref;
    int min_params = -1;
    bool variadic = false;
    int variadic_param_index = -1;
    bool has_native_template_return_spelling = false;
};

struct Symbols {
    const ModuleAst* module_tree = nullptr;
    std::set<std::string> types;
    std::set<std::string> generic_params;
    std::map<std::string, TypeRef> alias_type_refs;
    std::map<std::string, std::vector<std::string>> alias_generic_params;
    std::map<std::string, std::vector<TypeRef>> alias_generic_defaults;
    std::map<std::string, FunctionSignature> function_signatures;
    std::map<std::string, const FunctionDecl*> function_decls;
    std::map<std::string, std::vector<FunctionSignature>> function_overload_signatures;
    std::map<std::string, std::vector<const FunctionDecl*>> function_overload_decls;
    std::map<std::string, std::vector<FunctionSignature>> native_function_signatures;
    std::map<std::string, std::vector<const NativeFunctionDecl*>> native_function_decls;
    std::set<std::string> native_path_prefixes;
    std::set<std::string> module_import_prefixes;
    std::set<std::string> native_explicit_template_prefixes;
    std::set<std::string> native_types;
    std::map<std::string, const NativeTypeDecl*> native_type_decls;
    std::map<std::string, TypeRef> native_value_type_refs;
    std::set<std::string> native_enum_values;
    std::map<std::string, const EnumDecl*> enums;
    std::map<std::string, ClassDecl> native_classes;
    std::map<std::string, std::vector<ClassDecl>> native_class_specializations;
    std::map<std::string, const ClassDecl*> classes;
};

std::string trim(std::string text);
std::string base_type(const TypeRef& type);
bool known_type_ref(const Symbols& symbols, const TypeRef& type);
std::optional<std::pair<std::string, SourceLocation>> unknown_type_ref(const Symbols& symbols,
                                                                       const TypeRef& type);
void check_known_type_ref(const Symbols& symbols, const SourceLocation& location,
                          const TypeRef& type, const std::string& message);
TypeRef resolve_alias_ref(const Symbols& symbols, TypeRef type);
std::vector<std::string> split_cpp_escape_top_level(std::string text);
size_t find_cpp_escape_top_level_char(const std::string& text, char wanted);
Symbols collect_symbols(const ModuleAst& module);
void check_declarations(const ModuleAst& module, const Symbols& symbols);

} // namespace dudu
