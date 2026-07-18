#pragma once

#include "dudu/sema/sema_context.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dudu {

using NativeTemplateBindings = std::map<std::string, TypeRef>;
using NativePackBindingMap = std::map<std::string, std::vector<TypeRef>>;
using NativeTemplateParameterNames = std::set<std::string>;

NativeTemplateParameterNames native_type_template_parameters(const FunctionSignature& signature);
NativeTemplateParameterNames native_template_parameters(const FunctionSignature& signature);
std::optional<std::string>
native_template_pack_placeholder(const TypeRef& type,
                                 const NativeTemplateParameterNames& template_params);
bool bind_native_template_type_ast(const TypeRef& expected, const TypeRef& got,
                                   const NativeTemplateParameterNames& template_params,
                                   NativeTemplateBindings& bindings);
bool bind_native_template_type_ast(const Symbols& symbols, const TypeRef& expected,
                                   const TypeRef& got,
                                   const NativeTemplateParameterNames& template_params,
                                   NativeTemplateBindings& bindings);
bool bind_native_template_type_ast(const Symbols& symbols, const TypeRef& expected,
                                   const TypeRef& got,
                                   const NativeTemplateParameterNames& template_params,
                                   NativeTemplateBindings& bindings,
                                   NativePackBindingMap& pack_bindings);

} // namespace dudu
