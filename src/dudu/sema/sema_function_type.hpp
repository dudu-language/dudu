#pragma once

#include "dudu/sema/sema_context.hpp"

#include <string>
#include <vector>

namespace dudu {

std::string function_type(const FunctionSignature& signature);
FunctionSignature function_signature_from_decl(const FunctionDecl& fn);
TypeRef function_type_ref(const FunctionSignature& signature, SourceLocation location = {});
void set_signature_param_types(FunctionSignature& signature, std::vector<TypeRef> types);
void set_signature_return_type(FunctionSignature& signature, TypeRef type);
size_t signature_param_count(const FunctionSignature& signature);
size_t signature_min_arg_count(const FunctionSignature& signature);
TypeRef signature_param_type_ref(const FunctionSignature& signature, size_t index);
TypeRef signature_return_type_ref(const FunctionSignature& signature);
size_t signature_variadic_param_index(const FunctionSignature& signature);
size_t signature_fixed_param_count(const FunctionSignature& signature, size_t arg_count);
size_t signature_param_index_for_arg(const FunctionSignature& signature, size_t arg_index,
                                     size_t arg_count);
bool parse_function_type(const TypeRef& type, FunctionSignature& out);
bool parse_function_type_or_alias(const Symbols& symbols, const TypeRef& type,
                                  FunctionSignature& out);

} // namespace dudu
