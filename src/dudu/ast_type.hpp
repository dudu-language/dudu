#pragma once

#include "dudu/ast.hpp"

#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::vector<std::string> template_type_arg_texts(const TypeRef& type, std::string_view name);
std::vector<std::string> template_type_arg_texts(std::string_view type, std::string_view name);
std::vector<TypeRef> template_type_arg_refs(const TypeRef& type, std::string_view name);
std::vector<TypeRef>
template_type_arg_refs_resolved(const TypeRef& type, std::string_view name,
                                const std::map<std::string, std::string>& aliases);
std::optional<std::string> first_template_type_arg_text(const TypeRef& type);
std::optional<std::string> first_template_type_arg_text(std::string_view type);
std::optional<std::string> single_template_type_arg_text(const TypeRef& type,
                                                         std::string_view name);
std::optional<std::string> single_template_type_arg_text(std::string_view type,
                                                         std::string_view name);
std::optional<std::string> unary_type_child_text(const TypeRef& type, TypeKind kind);
std::optional<std::string> unary_type_child_text(std::string_view type, TypeKind kind);
std::optional<std::string> unary_type_child_text(const TypeRef& type,
                                                 std::initializer_list<TypeKind> kinds);
std::optional<std::string> unary_type_child_text(std::string_view type,
                                                 std::initializer_list<TypeKind> kinds);
bool type_ref_contains_kind(const TypeRef& type, TypeKind kind);
std::string type_ref_head_name(const TypeRef& type);
std::string type_ref_text(const TypeRef& type);
bool has_type_ref(const TypeRef& type);
TypeRef void_type_ref(SourceLocation location = {});
bool function_has_return_type(const FunctionDecl& fn);
TypeRef function_return_type_ref(const FunctionDecl& fn);
std::string function_return_type_text(const FunctionDecl& fn);
TypeRef substitute_type_ref(const TypeRef& type,
                            const std::map<std::string, std::string>& substitutions);
std::string substitute_type_ref_text(const TypeRef& type,
                                     const std::map<std::string, std::string>& substitutions);

} // namespace dudu
