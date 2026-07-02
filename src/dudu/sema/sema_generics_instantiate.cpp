#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_generics_detail.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <utility>

namespace dudu {

FunctionSignature instantiate_generic_signature(const FunctionDecl& fn,
                                                const std::vector<TypeRef>& args) {
    const GenericTypeBindings substitutions = generic_type_bindings(fn.generic_params, args);
    FunctionSignature signature;
    set_signature_return_type(signature,
                              function_has_return_type(fn)
                                  ? substitute_generic_type_ref(fn.return_type_ref, substitutions)
                                  : void_type_ref(fn.location));
    std::vector<TypeRef> param_types;
    param_types.reserve(fn.params.size());
    for (const ParamDecl& param : fn.params) {
        TypeRef param_type = substitute_generic_type_ref(param.type_ref, substitutions);
        if (param.variadic &&
            generic_pack_param_named(fn.generic_params, type_ref_head_name(param.type_ref))) {
            param_type = named_type_ref("auto", param.location);
        }
        param_types.push_back(std::move(param_type));
    }
    set_signature_param_types(signature, std::move(param_types));
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (fn.params[i].variadic) {
            signature.variadic = true;
            signature.variadic_param_index = static_cast<int>(i);
            break;
        }
    }
    return signature;
}

ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name) {
    const GenericTypeBindings substitutions = generic_type_bindings(klass.generic_params, args);
    klass.name = instantiated_name;
    for (BaseClassDecl& base : klass.base_class_refs) {
        base.type_ref = substitute_generic_type_ref(base.type_ref, substitutions);
    }
    for (FieldDecl& field : klass.fields) {
        field.type_ref = substitute_generic_type_ref(field.type_ref, substitutions);
    }
    for (ConstDecl& field : klass.static_fields) {
        field.type_ref = substitute_generic_type_ref(field.type_ref, substitutions);
    }
    for (ConstDecl& constant : klass.constants) {
        constant.type_ref = substitute_generic_type_ref(constant.type_ref, substitutions);
    }
    for (FunctionDecl& method : klass.methods) {
        if (function_has_return_type(method)) {
            method.return_type_ref =
                substitute_generic_type_ref(method.return_type_ref, substitutions);
        }
        for (ParamDecl& param : method.params) {
            param.type_ref = substitute_generic_type_ref(param.type_ref, substitutions);
        }
    }
    return klass;
}

bool known_template_constructor_type(const FunctionScope& scope, const TypeRef& callee_type) {
    const std::string base = base_type(callee_type);
    if (base.find('.') != std::string::npos || base.find("::") != std::string::npos) {
        return scope.symbols.types.contains(base) || scope.symbols.native_classes.contains(base) ||
               class_for_receiver_type(scope.symbols, callee_type) != nullptr;
    }
    return known_type_ref(scope.symbols, callee_type) ||
           class_for_receiver_type(scope.symbols, callee_type) != nullptr;
}

} // namespace dudu
