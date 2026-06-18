#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_options.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::string lower_raw_cpp_escape_expr(std::string expr);
std::string lower_raw_cpp_escape_expr(std::string expr,
                                      const std::vector<std::string>& namespace_aliases);
std::string cpp_escape_body(std::string text);
std::vector<std::string> cpp_escape_lines(std::string body_text);
std::string lower_cpp_type(const std::string& raw_type);
std::string lower_cpp_type(const std::string& raw_type, const CppEmitOptions& options);
std::string lower_cpp_type(const std::string& raw_type,
                           const std::vector<std::string>& namespace_aliases);
std::string lower_cpp_type(const std::string& raw_type,
                           const std::vector<std::string>& namespace_aliases,
                           const CppEmitOptions& options);
std::string lower_cpp_type(const TypeRef& type);
std::string lower_cpp_type(const TypeRef& type, const CppEmitOptions& options);
std::string lower_cpp_type(const TypeRef& type, const std::vector<std::string>& namespace_aliases);
std::string lower_cpp_type(const TypeRef& type, const std::vector<std::string>& namespace_aliases,
                           const CppEmitOptions& options);
std::string lower_cpp_pointer_type(const std::string& pointee);
std::string lower_cpp_pointer_type(const std::string& pointee,
                                   const std::vector<std::string>& namespace_aliases);
std::string lower_enum_access(std::string expr);
std::string lower_generic_type_constructor(std::string expr);
std::string lower_len_calls(std::string expr);
std::string lower_numeric_separators(std::string expr);
std::string lower_pointer_cast_calls(std::string expr);
std::string lower_str_calls(std::string expr);
std::string lower_template_call_arg(const std::string& arg,
                                    const std::vector<std::string>& namespace_aliases);
std::string qualify_namespace_aliases(std::string expr,
                                      const std::vector<std::string>& namespace_aliases);
std::string replace_all(std::string text, std::string_view from, std::string_view to);
std::string replace_dots(std::string text);
std::string trim_copy(std::string text);
bool starts_with(std::string_view text, std::string_view prefix);
bool ends_with(std::string_view text, std::string_view suffix);
std::vector<std::string> split_top_level_args(const std::string& args);

} // namespace dudu
