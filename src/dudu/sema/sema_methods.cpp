#include "dudu/sema/sema_methods.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace dudu {
namespace {

bool method_is_static(const FunctionDecl& method) {
    return method.params.empty() || method.params.front().name != "self";
}

TypeRef self_type_ref_for_method(const ClassDecl& klass, const std::vector<TypeRef>& receiver_args,
                                 SourceLocation location) {
    if (receiver_args.empty()) {
        return named_type_ref(klass.name, location);
    }
    TypeRef type = named_type_ref(klass.name, location);
    type.kind = TypeKind::Template;
    type.children = receiver_args;
    return type;
}

TypeRef instantiate_method_type_ref(const Symbols& symbols, const ClassDecl& klass,
                                    const FunctionDecl& method, const TypeRef& type,
                                    const std::vector<TypeRef>& receiver_args,
                                    const std::vector<TypeRef>& method_args) {
    TypeRef out = substitute_generic_type_ref(method.generic_params, method_args, type);
    out = substitute_type_ref(
        out, {{"Self", self_type_ref_for_method(klass, receiver_args, type.location)}});
    return substitute_receiver_template_type(out, symbols, klass, receiver_args);
}

FunctionSignature instantiate_method_signature(const Symbols& symbols, const ClassDecl& klass,
                                               const FunctionDecl& method,
                                               const std::vector<TypeRef>& receiver_args,
                                               const std::vector<TypeRef>& method_args);

bool method_signature_for_type_impl(const Symbols& symbols, const TypeRef& receiver_type,
                                    const std::string& lookup_name,
                                    const std::vector<TypeRef>& method_args,
                                    const std::string& display_name, FunctionSignature& signature,
                                    const SourceLocation* location) {
    const TypeRef templated_receiver = receiver_template_type_ref(symbols, receiver_type);
    const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(templated_receiver);
    const std::string type = receiver_class_name(symbols, receiver_type);
    const ClassDecl* klass = class_for_receiver_type(symbols, receiver_type);
    if (klass == nullptr) {
        return false;
    }
    bool saw_name = false;
    size_t expected_type_args = 0;
    for (const FunctionDecl& method : klass->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        saw_name = true;
        expected_type_args = generic_min_arity(method.generic_params);
        if (!generic_arity_matches(method.generic_params, method_args.size())) {
            continue;
        }
        signature =
            instantiate_method_signature(symbols, *klass, method, receiver_args, method_args);
        return true;
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        if (method_signature_for_type_impl(symbols, base_decl.type_ref, lookup_name, method_args,
                                           display_name, signature, nullptr)) {
            return true;
        }
    }
    if (saw_name && location != nullptr) {
        sema_fail(*location, "method " + type + "." + lookup_name + " expects " +
                                 std::to_string(expected_type_args) + " type arguments, got " +
                                 std::to_string(method_args.size()));
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown method: " + type + "." + display_name);
    }
    return false;
}

std::vector<FunctionSignature>
method_signatures_for_type_impl(const Symbols& symbols, const TypeRef& receiver_type,
                                const std::string& lookup_name,
                                const std::vector<TypeRef>& method_args) {
    std::vector<FunctionSignature> out;
    const TypeRef templated_receiver = receiver_template_type_ref(symbols, receiver_type);
    const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(templated_receiver);
    const ClassDecl* klass = class_for_receiver_type(symbols, receiver_type);
    if (klass == nullptr) {
        return out;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        if (!generic_arity_matches(method.generic_params, method_args.size())) {
            continue;
        }
        out.push_back(
            instantiate_method_signature(symbols, *klass, method, receiver_args, method_args));
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        std::vector<FunctionSignature> base_signatures =
            method_signatures_for_type_impl(symbols, base_decl.type_ref, lookup_name, method_args);
        out.insert(out.end(), base_signatures.begin(), base_signatures.end());
    }
    return out;
}

std::vector<DuduMethodInstantiation>
dudu_method_instantiations_for_type_impl(const Symbols& symbols, const TypeRef& receiver_type,
                                         const std::string& method_name,
                                         const std::vector<TypeRef>& method_args) {
    std::vector<DuduMethodInstantiation> out;
    const TypeRef concrete_receiver = receiver_template_type_ref(symbols, receiver_type);
    const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(concrete_receiver);
    const ClassDecl* klass = class_for_receiver_type(symbols, concrete_receiver);
    if (klass == nullptr) {
        return out;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (method.name != method_name ||
            !generic_arity_matches(method.generic_params, method_args.size())) {
            continue;
        }
        out.push_back({.owner = klass,
                       .method = &method,
                       .receiver_type = concrete_receiver,
                       .receiver_args = receiver_args,
                       .method_args = method_args,
                       .signature = instantiate_method_signature(symbols, *klass, method,
                                                                 receiver_args, method_args)});
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        const TypeRef base_type =
            substitute_generic_type_ref(klass->generic_params, receiver_args, base_decl.type_ref);
        std::vector<DuduMethodInstantiation> base =
            dudu_method_instantiations_for_type_impl(symbols, base_type, method_name, method_args);
        out.insert(out.end(), base.begin(), base.end());
    }
    return out;
}

bool static_method_signature_for_type_impl(const Symbols& symbols, const TypeRef& type_name,
                                           const std::string& lookup_name,
                                           const std::vector<TypeRef>& method_args,
                                           const std::string& display_name,
                                           FunctionSignature& signature,
                                           const SourceLocation* location) {
    const TypeRef templated_receiver = receiver_template_type_ref(symbols, type_name);
    const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(templated_receiver);
    const std::string type = receiver_class_name(symbols, type_name);
    const ClassDecl* klass = class_for_receiver_type(symbols, type_name);
    if (klass == nullptr) {
        return false;
    }
    bool saw_name = false;
    size_t expected_type_args = 0;
    for (const FunctionDecl& method : klass->methods) {
        if (method.name != lookup_name || !method_is_static(method)) {
            continue;
        }
        saw_name = true;
        expected_type_args = generic_min_arity(method.generic_params);
        if (!generic_arity_matches(method.generic_params, method_args.size())) {
            continue;
        }
        signature =
            instantiate_method_signature(symbols, *klass, method, receiver_args, method_args);
        return true;
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        if (static_method_signature_for_type_impl(symbols, base_decl.type_ref, lookup_name,
                                                  method_args, display_name, signature, nullptr)) {
            return true;
        }
    }
    if (saw_name && location != nullptr) {
        sema_fail(*location, "method " + type + "." + lookup_name + " expects " +
                                 std::to_string(expected_type_args) + " type arguments, got " +
                                 std::to_string(method_args.size()));
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown static method: " + type + "." + display_name);
    }
    return false;
}

FunctionSignature instantiate_method_signature(const Symbols& symbols, const ClassDecl& klass,
                                               const FunctionDecl& method,
                                               const std::vector<TypeRef>& receiver_args,
                                               const std::vector<TypeRef>& method_args) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    std::vector<TypeRef> param_types;
    param_types.reserve(method.params.size() - first_param);
    for (size_t i = first_param; i < method.params.size(); ++i) {
        TypeRef param_type = instantiate_method_type_ref(
            symbols, klass, method, method.params[i].type_ref, receiver_args, method_args);
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
    set_signature_return_type(
        signature, function_has_return_type(method)
                       ? instantiate_method_type_ref(symbols, klass, method, method.return_type_ref,
                                                     receiver_args, method_args)
                       : void_type_ref(method.location));
    return signature;
}

} // namespace

bool method_signature_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, signature)) {
        return true;
    }
    return method_signature_for_type_impl(symbols, receiver_type, method_name, {}, method_name,
                                          signature, location);
}

bool method_signature_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                               const std::string& method_name,
                               const std::vector<TypeRef>& method_args,
                               FunctionSignature& signature, const SourceLocation* location) {
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, signature)) {
        return true;
    }
    return method_signature_for_type_impl(symbols, receiver_type, method_name, method_args,
                                          method_name, signature, location);
}

std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location) {
    const auto resolved = inferred_dudu_method_instantiation_for_type(scope, receiver_type,
                                                                      method_name, args, location);
    return resolved ? std::optional<FunctionSignature>{resolved->signature} : std::nullopt;
}

std::vector<DuduMethodInstantiation>
dudu_method_instantiations_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                    const std::string& method_name,
                                    const std::vector<TypeRef>& method_args) {
    return dudu_method_instantiations_for_type_impl(symbols, receiver_type, method_name,
                                                    method_args);
}

std::optional<DuduMethodInstantiation> inferred_dudu_method_instantiation_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location) {
    const TypeRef concrete_receiver = receiver_template_type_ref(scope.symbols, receiver_type);
    const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(concrete_receiver);
    const ClassDecl* klass = class_for_receiver_type(scope.symbols, concrete_receiver);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (method.name != method_name || method.generic_params.empty()) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        const auto inferred = infer_generic_method_type_args(
            scope, method,
            receiver_class_name(scope.symbols, concrete_receiver) + "." + method_name, args,
            first_param, location, &concrete_receiver);
        if (!inferred) {
            return std::nullopt;
        }
        return DuduMethodInstantiation{
            .owner = klass,
            .method = &method,
            .receiver_type = concrete_receiver,
            .receiver_args = receiver_args,
            .method_args = *inferred,
            .signature = instantiate_method_signature(scope.symbols, *klass, method, receiver_args,
                                                      *inferred)};
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        const TypeRef base_type =
            substitute_generic_type_ref(klass->generic_params, receiver_args, base_decl.type_ref);
        if (const auto found = inferred_dudu_method_instantiation_for_type(
                scope, base_type, method_name, args, nullptr)) {
            return found;
        }
    }
    return std::nullopt;
}

std::optional<DuduMethodInstantiation> inferred_dudu_method_instantiation_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const std::optional<TypeRef>& expected_return,
    const SourceLocation* location) {
    const TypeRef concrete_receiver = receiver_template_type_ref(scope.symbols, receiver_type);
    const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(concrete_receiver);
    const ClassDecl* klass = class_for_receiver_type(scope.symbols, concrete_receiver);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (method.name != method_name || method.generic_params.empty()) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        std::vector<TypeRef> arg_types;
        arg_types.reserve(args.size());
        for (const Expr& arg : args) {
            arg_types.push_back(infer_expr_type_ast(scope, arg, location));
        }
        const auto inferred = infer_generic_method_type_args_from_type_refs(
            method, receiver_class_name(scope.symbols, concrete_receiver) + "." + method_name,
            arg_types, first_param, expected_return, location, &concrete_receiver);
        if (!inferred) {
            return std::nullopt;
        }
        return DuduMethodInstantiation{
            .owner = klass,
            .method = &method,
            .receiver_type = concrete_receiver,
            .receiver_args = receiver_args,
            .method_args = *inferred,
            .signature = instantiate_method_signature(scope.symbols, *klass, method, receiver_args,
                                                      *inferred)};
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        const TypeRef base_type =
            substitute_generic_type_ref(klass->generic_params, receiver_args, base_decl.type_ref);
        if (const auto found = inferred_dudu_method_instantiation_for_type(
                scope, base_type, method_name, args, expected_return, nullptr)) {
            return found;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const std::optional<TypeRef>& expected_return,
    const SourceLocation* location) {
    const auto resolved = inferred_dudu_method_instantiation_for_type(
        scope, receiver_type, method_name, args, expected_return, location);
    return resolved ? std::optional<FunctionSignature>{resolved->signature} : std::nullopt;
}

std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          const TypeRef& receiver_type,
                                                          const std::string& method_name) {
    FunctionSignature builtin;
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, builtin)) {
        return {builtin};
    }
    return method_signatures_for_type_impl(symbols, receiver_type, method_name, {});
}

std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          const TypeRef& receiver_type,
                                                          const std::string& method_name,
                                                          const std::vector<TypeRef>& method_args) {
    FunctionSignature builtin;
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, builtin)) {
        return {builtin};
    }
    return method_signatures_for_type_impl(symbols, receiver_type, method_name, method_args);
}

bool static_method_signature_for_type(const Symbols& symbols, const TypeRef& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location) {
    return static_method_signature_for_type_impl(symbols, type_name, method_name, {}, method_name,
                                                 signature, location);
}

bool static_method_signature_for_type(const Symbols& symbols, const TypeRef& type_name,
                                      const std::string& method_name,
                                      const std::vector<TypeRef>& method_args,
                                      FunctionSignature& signature,
                                      const SourceLocation* location) {
    return static_method_signature_for_type_impl(symbols, type_name, method_name, method_args,
                                                 method_name, signature, location);
}

} // namespace dudu
