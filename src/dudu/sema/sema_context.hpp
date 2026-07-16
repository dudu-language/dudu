#pragma once

#include "dudu/core/ast.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {

struct FunctionSignature {
    std::vector<std::string> template_params;
    std::vector<bool> template_param_is_value;
    TypeRef receiver_type_ref;
    std::vector<TypeRef> param_type_refs;
    TypeRef return_type_ref;
    int min_params = -1;
    bool variadic = false;
    bool deleted = false;
    int variadic_param_index = -1;
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
    std::map<std::string, std::vector<std::string>> native_function_identities_by_binding;
    std::map<std::string, std::map<std::string, const NativeFunctionDecl*>>
        native_function_decls_by_identity;
    std::set<std::string> native_path_prefixes;
    std::set<std::string> module_import_prefixes;
    std::set<std::string> native_types;
    std::map<std::string, std::string> native_type_identity_by_binding;
    std::map<std::string, std::map<std::string, const NativeTypeDecl*>>
        native_type_decls_by_identity;
    std::map<std::string, std::map<std::string, const ClassDecl*>> native_class_decls_by_identity;
    std::map<std::string, TypeRef> native_value_type_refs;
    std::set<std::string> native_enum_values;
    std::map<std::string, const EnumDecl*> enums;
    std::map<std::string, std::vector<ClassDecl>> native_class_specializations;
    std::map<std::string, const ClassDecl*> classes;
};

std::string base_type(const TypeRef& type);
bool known_type_ref(const Symbols& symbols, const TypeRef& type);
std::optional<std::pair<std::string, SourceLocation>> unknown_type_ref(const Symbols& symbols,
                                                                       const TypeRef& type);
void check_known_type_ref(const Symbols& symbols, const SourceLocation& location,
                          const TypeRef& type, const std::string& message);
TypeRef resolve_alias_ref(const Symbols& symbols, TypeRef type);
TypeRef canonical_native_type_ref(const Symbols& symbols, TypeRef type);
bool is_canonical_native_type_ref(const TypeRef& type);
const NativeTypeDecl* native_type_decl_for_binding(const Symbols& symbols,
                                                   std::string_view binding);
const ClassDecl* native_class_decl_for_binding(const Symbols& symbols, std::string_view binding);
bool is_native_class_binding(const Symbols& symbols, std::string_view binding);
const NativeFunctionDecl* native_function_decl_for_overload(const Symbols& symbols,
                                                            std::string_view binding,
                                                            size_t overload_index);
std::vector<const NativeFunctionDecl*> native_function_decls_for_binding(const Symbols& symbols,
                                                                         std::string_view binding);
std::vector<std::string> split_cpp_escape_top_level(std::string text);
size_t find_cpp_escape_top_level_char(const std::string& text, char wanted);
Symbols collect_symbols(const ModuleAst& module);
void check_declarations(const ModuleAst& module, const Symbols& symbols);

} // namespace dudu
