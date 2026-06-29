#pragma once

#include "dudu/sema/sema_context.hpp"

#include <map>
#include <optional>
#include <string>

namespace dudu {

using NativeTemplateBindings = std::map<std::string, TypeRef>;

bool native_template_placeholder(const std::string& type);
std::optional<std::string> native_template_pack_placeholder(const TypeRef& type);
bool bind_native_template_type_ast(const TypeRef& expected, const TypeRef& got,
                                   NativeTemplateBindings& bindings);
bool bind_native_template_type_ast(const Symbols& symbols, const TypeRef& expected,
                                   const TypeRef& got, NativeTemplateBindings& bindings);

} // namespace dudu
