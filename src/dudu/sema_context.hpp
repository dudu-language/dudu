#pragma once

#include "dudu/ast.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dudu {

struct FunctionSignature {
    std::vector<std::string> params;
    std::string return_type;
    bool variadic = false;
};

struct Symbols {
    std::set<std::string> types;
    std::set<std::string> generic_params;
    std::map<std::string, std::string> aliases;
    std::map<std::string, TypeRef> alias_type_refs;
    std::map<std::string, std::string> functions;
    std::map<std::string, FunctionSignature> function_signatures;
    std::map<std::string, const FunctionDecl*> function_decls;
    std::map<std::string, std::vector<FunctionSignature>> native_function_signatures;
    std::set<std::string> native_import_prefixes;
    std::set<std::string> native_explicit_template_prefixes;
    std::set<std::string> native_types;
    std::map<std::string, std::string> native_values;
    std::map<std::string, const EnumDecl*> enums;
    std::map<std::string, ClassDecl> native_classes;
    std::map<std::string, const ClassDecl*> classes;
};

std::string trim(std::string text);
std::string base_type(std::string type);
bool known_type(const Symbols& symbols, const std::string& type);
std::optional<std::pair<std::string, SourceLocation>> unknown_type_ref(const Symbols& symbols,
                                                                       const TypeRef& type);
void check_known_type_ref(const Symbols& symbols, const SourceLocation& location,
                          const TypeRef& type, const std::string& message);
std::string resolve_alias(const Symbols& symbols, std::string type);
std::vector<std::string> split_top_level(std::string text);
size_t find_top_level_char(const std::string& text, char wanted);
std::vector<std::string> tuple_types(const Symbols& symbols, std::string type);
Symbols collect_symbols(const ModuleAst& module);
void check_declarations(const ModuleAst& module, const Symbols& symbols);

} // namespace dudu
