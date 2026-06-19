#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_options.hpp"

#include <string>
#include <vector>

namespace dudu {

std::string strip_c_import_type_aliases(std::string type,
                                        const std::vector<std::string>& namespace_aliases);

std::string lower_cpp_type_name(std::string name);
std::string lower_cpp_type_name(std::string name,
                                const std::vector<std::string>& namespace_aliases);
std::string lower_cpp_type_name(std::string name,
                                const std::vector<std::string>& namespace_aliases,
                                const CppEmitOptions& options);

std::string lower_cpp_function_type(const TypeRef& type, bool pointer);
std::string lower_cpp_function_type(const TypeRef& type, bool pointer,
                                    const std::vector<std::string>& namespace_aliases);
std::string lower_cpp_function_type(const TypeRef& type, bool pointer,
                                    const std::vector<std::string>& namespace_aliases,
                                    const CppEmitOptions& options);

std::string lower_template_type(const TypeRef& type);
std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases);
std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases,
                                const CppEmitOptions& options);

std::string lower_fixed_array_type(const TypeRef& type);
std::string lower_fixed_array_type(const TypeRef& type,
                                   const std::vector<std::string>& namespace_aliases);
std::string lower_fixed_array_type(const TypeRef& type,
                                   const std::vector<std::string>& namespace_aliases,
                                   const CppEmitOptions& options);

} // namespace dudu
