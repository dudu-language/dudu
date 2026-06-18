#pragma once

#include "dudu/sema_context.hpp"

#include <string>
#include <vector>

namespace dudu {

std::string function_type(const FunctionSignature& signature);
TypeRef function_type_ref(const FunctionSignature& signature, SourceLocation location = {});
void set_signature_param_types(FunctionSignature& signature, std::vector<TypeRef> types);
void set_signature_return_type(FunctionSignature& signature, TypeRef type);
TypeRef signature_param_type_ref(const FunctionSignature& signature, size_t index);
TypeRef signature_return_type_ref(const FunctionSignature& signature);
std::string signature_return_type_text(const FunctionSignature& signature);
bool parse_function_type(const TypeRef& type, FunctionSignature& out);

} // namespace dudu
