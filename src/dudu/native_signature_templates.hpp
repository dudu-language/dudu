#pragma once

#include "dudu/sema_context.hpp"

#include <map>
#include <optional>
#include <string>

namespace dudu {

using NativeTemplateBindings = std::map<std::string, std::string>;

bool native_template_placeholder(const std::string& type);
std::optional<std::string> native_template_pack_placeholder(std::string type);
bool bind_native_template_type(std::string expected, std::string got,
                               NativeTemplateBindings& bindings);
bool bind_native_template_type_ast(const TypeRef& expected, const TypeRef& got,
                                   NativeTemplateBindings& bindings);
bool bind_native_template_type_ast(const Symbols& symbols, const std::string& expected,
                                   const std::string& got, NativeTemplateBindings& bindings);

} // namespace dudu
