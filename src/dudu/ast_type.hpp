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
std::string substitute_type_ref_text(const TypeRef& type,
                                     const std::map<std::string, std::string>& substitutions);

} // namespace dudu
