#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_generics_detail.hpp"

#include <vector>

namespace dudu {

struct NativeMethodOwner {
    const ClassDecl* declaration = nullptr;
    TypeRef receiver_type;
    GenericTypeBindings specialization_bindings;
    bool specialized = false;
};

NativeMethodOwner native_method_owner_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                               const ClassDecl& primary);

FunctionSignature instantiate_native_specialized_method_signature(
    const Symbols& symbols, const NativeMethodOwner& owner, const FunctionDecl& method,
    const std::vector<TypeRef>& method_args);

std::vector<TypeRef> native_method_owner_base_types(const NativeMethodOwner& owner);

} // namespace dudu
