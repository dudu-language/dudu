#include "dudu/sema/sema_native_methods.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_native_specializations.hpp"

#include <algorithm>
#include <utility>

namespace dudu {
namespace {

TypeRef instantiate_specialized_type(const Symbols& symbols, const NativeMethodOwner& owner,
                                     const FunctionDecl& method, const TypeRef& source,
                                     const std::vector<TypeRef>& method_args) {
    TypeRef type = substitute_generic_type_ref(method.generic_params, method_args, source);
    const GenericTypeBindings member_bindings = specialized_owner_member_bindings(
        *owner.declaration, owner.receiver_type, owner.specialization_bindings);
    type = substitute_native_specialization_type(type, member_bindings);
    type = substitute_type_ref(type, {{"Self", owner.receiver_type}});
    type =
        structure_owner_alias_templates(std::move(type), *owner.declaration, owner.receiver_type);
    if (!method.generic_params.empty() && method_args.empty()) {
        return type;
    }
    return resolve_associated_type_ref(symbols, type);
}

} // namespace

NativeMethodOwner native_method_owner_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                               const ClassDecl& primary) {
    NativeMethodOwner owner;
    owner.declaration = &primary;
    owner.receiver_type = receiver_type;
    if (!primary.native_declaration) {
        return owner;
    }
    GenericTypeBindings bindings;
    if (const ClassDecl* specialization =
            native_specialized_class_for_owner(symbols, receiver_type, bindings)) {
        owner.declaration = specialization;
        owner.specialization_bindings = std::move(bindings);
        owner.specialized = true;
    }
    return owner;
}

FunctionSignature instantiate_native_specialized_method_signature(
    const Symbols& symbols, const NativeMethodOwner& owner, const FunctionDecl& method,
    const std::vector<TypeRef>& method_args) {
    FunctionSignature signature;
    signature.template_params = method.generic_params;
    signature.template_param_is_value = method.generic_param_is_value;
    for (const TypeRef& default_arg : method.generic_default_args) {
        signature.template_default_args.push_back(resolve_associated_type_ref(
            symbols,
            instantiate_specialized_type(symbols, owner, method, default_arg, method_args)));
    }
    if (has_type_ref(method.receiver_type_ref)) {
        signature.receiver_type_ref = instantiate_specialized_type(
            symbols, owner, method, method.receiver_type_ref, method_args);
    }
    signature.deleted = method.deleted;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    if (method.min_params >= 0) {
        signature.min_params = std::max(0, method.min_params - static_cast<int>(first_param));
    }
    std::vector<TypeRef> param_types;
    param_types.reserve(method.params.size() - first_param);
    for (size_t i = first_param; i < method.params.size(); ++i) {
        TypeRef param_type = instantiate_specialized_type(symbols, owner, method,
                                                          method.params[i].type_ref, method_args);
        if (method.params[i].variadic &&
            generic_pack_param_named(method.generic_params,
                                     type_ref_head_name(method.params[i].type_ref))) {
            param_type = named_type_ref("auto", method.params[i].location);
        }
        param_types.push_back(std::move(param_type));
    }
    set_signature_param_types(signature, std::move(param_types));
    for (size_t i = first_param; i < method.params.size(); ++i) {
        if (method.params[i].variadic) {
            signature.variadic = true;
            signature.variadic_param_index = static_cast<int>(i - first_param);
            break;
        }
    }
    set_signature_return_type(signature, function_has_return_type(method)
                                             ? instantiate_specialized_type(symbols, owner, method,
                                                                            method.return_type_ref,
                                                                            method_args)
                                             : void_type_ref(method.location));
    return signature;
}

std::vector<TypeRef> native_method_owner_base_types(const NativeMethodOwner& owner) {
    std::vector<TypeRef> bases;
    if (owner.declaration == nullptr) {
        return bases;
    }
    bases.reserve(owner.declaration->base_class_refs.size());
    for (const BaseClassDecl& base : owner.declaration->base_class_refs) {
        bases.push_back(owner.specialized ? substitute_native_specialization_type(
                                                base.type_ref, owner.specialization_bindings)
                                          : base.type_ref);
    }
    return bases;
}

} // namespace dudu
