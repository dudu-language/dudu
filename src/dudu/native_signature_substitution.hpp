#pragma once

#include "dudu/native_signature_templates.hpp"
#include "dudu/sema_native.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

using NativePackBindingMap = std::map<std::string, std::vector<TypeRef>>;

bool native_index_placeholder(const std::string& name);
bool numeric_template_arg(std::string_view arg);
TypeRef native_template_binding_type_ref(std::string_view text, SourceLocation location = {});

std::optional<std::pair<std::string, std::vector<std::string>>>
native_template_call_base(const std::string& callee);

FunctionSignature substitute_explicit_template_signature(FunctionSignature signature,
                                                         const std::vector<std::string>& args);
FunctionSignature substitute_explicit_template_signature(FunctionSignature signature,
                                                         const std::vector<TypeRef>& args);

FunctionSignature substitute_bound_template_signature(FunctionSignature signature,
                                                      const NativeTemplateBindings& bindings,
                                                      const NativePackBindingMap& pack_bindings);

} // namespace dudu
