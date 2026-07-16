#pragma once

#include "dudu/native/native_signature_templates.hpp"
#include "dudu/sema/sema_native.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

using NativePackBindingMap = std::map<std::string, std::vector<TypeRef>>;

FunctionSignature substitute_explicit_template_signature(const Symbols& symbols,
                                                         FunctionSignature signature,
                                                         const std::vector<TypeRef>& args);

FunctionSignature substitute_bound_template_signature(const Symbols& symbols,
                                                      FunctionSignature signature,
                                                      const NativeTemplateBindings& bindings,
                                                      const NativePackBindingMap& pack_bindings);

} // namespace dudu
