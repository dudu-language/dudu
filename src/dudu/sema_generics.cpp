#include "dudu/sema_generics.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_expr.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_methods_internal.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace dudu {

namespace {

std::map<std::string, TypeRef> generic_substitutions(const std::vector<std::string>& params,
                                                     const std::vector<TypeRef>& args) {
    std::map<std::string, TypeRef> out;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        out.emplace(params[i], args[i]);
    }
    return out;
}

bool generic_param_named(const std::vector<std::string>& params, const std::string& name) {
    return std::find(params.begin(), params.end(), name) != params.end();
}

bool unresolved_generic_binding(const TypeRef& binding) {
    return !has_type_ref(binding) || type_ref_is_auto(binding) ||
           type_ref_is_name(binding, "list") || type_ref_is_name(binding, "dict") ||
           type_ref_is_name(binding, "set");
}

bool infer_generic_binding(const TypeRef& param_type, const TypeRef& arg_type,
                           const std::vector<std::string>& params,
                           std::map<std::string, TypeRef>& bindings, std::string& error) {
    const std::string param = type_ref_head_name(param_type);
    const std::string arg = type_ref_head_name(arg_type);
    if (generic_param_named(params, param)) {
        const auto [it, inserted] = bindings.emplace(param, arg_type);
        if (!inserted && !type_ref_equivalent(it->second, arg_type)) {
            error = "conflicting inferred type argument " + param + ": " +
                    substitute_type_ref_text(it->second, {}) + " vs " +
                    substitute_type_ref_text(arg_type, {});
            return false;
        }
        return true;
    }
    if (param.empty() || arg.empty()) {
        return true;
    }
    if ((param_type.kind == TypeKind::Pointer || param_type.kind == TypeKind::Reference) &&
        param_type.kind == arg_type.kind && param_type.children.size() == 1 &&
        arg_type.children.size() == 1) {
        return infer_generic_binding(param_type.children.front(), arg_type.children.front(), params,
                                     bindings, error);
    }
    if (param_type.kind != arg_type.kind) {
        return true;
    }
    if (param_type.kind == TypeKind::Template) {
        if (trim(param_type.name) != trim(arg_type.name) ||
            param_type.children.size() != arg_type.children.size()) {
            return true;
        }
        for (size_t i = 0; i < param_type.children.size(); ++i) {
            if (!infer_generic_binding(param_type.children[i], arg_type.children[i], params,
                                       bindings, error)) {
                return false;
            }
        }
        return true;
    }

    if (param_type.kind == TypeKind::FixedArray) {
        if (param_type.children.size() != 1 || arg_type.children.size() != 1) {
            return true;
        }
        return infer_generic_binding(param_type.children.front(), arg_type.children.front(), params,
                                     bindings, error);
    }

    if (param_type.children.empty() || param_type.children.size() != arg_type.children.size()) {
        return true;
    }
    for (size_t i = 0; i < param_type.children.size(); ++i) {
        if (!infer_generic_binding(param_type.children[i], arg_type.children[i], params, bindings,
                                   error)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<TypeRef> template_type_refs(const Expr& expr) {
    if (!expr.template_type_args.empty()) {
        return expr.template_type_args;
    }
    if (!expr.template_args.empty()) {
        sema_fail(expr.location, "malformed template call: missing parsed type arguments");
    }
    return {};
}

std::string template_args_lookup_text(const Expr& expr) {
    std::ostringstream out;
    const std::vector<TypeRef> args = template_type_refs(expr);
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(args[i], {});
    }
    return out.str();
}

std::optional<std::vector<TypeRef>> infer_generic_call_type_args(const FunctionScope& scope,
                                                                 const FunctionDecl& fn,
                                                                 const std::string& callee,
                                                                 const std::vector<Expr>& args,
                                                                 const SourceLocation* location) {
    if (fn.generic_params.empty()) {
        return std::nullopt;
    }
    if (location != nullptr && args.size() != fn.params.size()) {
        sema_fail(*location, "function " + callee + " expects " + std::to_string(fn.params.size()) +
                                 " arguments, got " + std::to_string(args.size()));
    }
    if (args.size() != fn.params.size()) {
        return std::nullopt;
    }
    std::map<std::string, TypeRef> bindings;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const TypeRef got = infer_expr_type_ast(scope, args[i], location);
        std::string error;
        if (!infer_generic_binding(fn.params[i].type_ref, got, fn.generic_params, bindings,
                                   error)) {
            if (location != nullptr) {
                sema_fail(node_location(*location, args[i]), error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    std::vector<TypeRef> out;
    out.reserve(fn.generic_params.size());
    for (const std::string& param : fn.generic_params) {
        const auto binding = bindings.find(param);
        if (binding == bindings.end() || unresolved_generic_binding(binding->second)) {
            if (location != nullptr) {
                sema_fail(*location, "cannot infer type argument " + param + " for " + callee);
            }
            return std::nullopt;
        }
        out.push_back(binding->second);
    }
    return out;
}

std::optional<std::vector<TypeRef>>
infer_generic_method_type_args(const FunctionScope& scope, const FunctionDecl& method,
                               const std::string& callee, const std::vector<Expr>& args,
                               size_t first_param, const SourceLocation* location) {
    if (method.generic_params.empty()) {
        return std::nullopt;
    }
    const size_t expected_args =
        method.params.size() >= first_param ? method.params.size() - first_param : 0;
    if (location != nullptr && args.size() != expected_args) {
        sema_fail(*location, "method " + callee + " expects " + std::to_string(expected_args) +
                                 " arguments, got " + std::to_string(args.size()));
    }
    if (args.size() != expected_args) {
        return std::nullopt;
    }
    std::vector<TypeRef> arg_types;
    arg_types.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        arg_types.push_back(infer_expr_type_ast(scope, args[i], location));
    }
    return infer_generic_method_type_args_from_type_refs(method, callee, arg_types, first_param,
                                                         std::nullopt, location);
}

std::optional<std::vector<TypeRef>> infer_generic_method_type_args_from_type_refs(
    const FunctionDecl& method, const std::string& callee, const std::vector<TypeRef>& arg_types,
    size_t first_param, const std::optional<TypeRef>& expected_return,
    const SourceLocation* location) {
    if (method.generic_params.empty()) {
        return std::nullopt;
    }
    const size_t expected_args =
        method.params.size() >= first_param ? method.params.size() - first_param : 0;
    if (location != nullptr && arg_types.size() != expected_args) {
        sema_fail(*location, "method " + callee + " expects " + std::to_string(expected_args) +
                                 " arguments, got " + std::to_string(arg_types.size()));
    }
    if (arg_types.size() != expected_args) {
        return std::nullopt;
    }
    std::map<std::string, TypeRef> bindings;
    for (size_t i = 0; i < arg_types.size(); ++i) {
        std::string error;
        if (!infer_generic_binding(method.params[first_param + i].type_ref, arg_types[i],
                                   method.generic_params, bindings, error)) {
            if (location != nullptr) {
                sema_fail(*location, error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    if (expected_return && has_type_ref(*expected_return) && has_type_ref(method.return_type_ref)) {
        std::string error;
        if (!infer_generic_binding(method.return_type_ref, *expected_return, method.generic_params,
                                   bindings, error)) {
            if (location != nullptr) {
                sema_fail(*location, error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    std::vector<TypeRef> out;
    out.reserve(method.generic_params.size());
    for (const std::string& param : method.generic_params) {
        const auto binding = bindings.find(param);
        if (binding == bindings.end() || unresolved_generic_binding(binding->second)) {
            if (location != nullptr) {
                sema_fail(*location, "cannot infer type argument " + param + " for " + callee);
            }
            return std::nullopt;
        }
        out.push_back(binding->second);
    }
    return out;
}

FunctionSignature instantiate_generic_signature(const FunctionDecl& fn,
                                                const std::vector<TypeRef>& args) {
    const std::map<std::string, TypeRef> substitutions =
        generic_substitutions(fn.generic_params, args);
    FunctionSignature signature;
    set_signature_return_type(signature,
                              function_has_return_type(fn)
                                  ? substitute_type_ref(fn.return_type_ref, substitutions)
                                  : void_type_ref(fn.location));
    std::vector<TypeRef> param_types;
    param_types.reserve(fn.params.size());
    for (const ParamDecl& param : fn.params) {
        param_types.push_back(substitute_type_ref(param.type_ref, substitutions));
    }
    set_signature_param_types(signature, std::move(param_types));
    return signature;
}

ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name) {
    const std::map<std::string, TypeRef> substitutions =
        generic_substitutions(klass.generic_params, args);
    klass.name = instantiated_name;
    for (BaseClassDecl& base : klass.base_class_refs) {
        base.type_ref = substitute_type_ref(base.type_ref, substitutions);
    }
    for (FieldDecl& field : klass.fields) {
        field.type_ref = substitute_type_ref(field.type_ref, substitutions);
    }
    for (ConstDecl& field : klass.static_fields) {
        field.type_ref = substitute_type_ref(field.type_ref, substitutions);
    }
    for (ConstDecl& constant : klass.constants) {
        constant.type_ref = substitute_type_ref(constant.type_ref, substitutions);
    }
    for (FunctionDecl& method : klass.methods) {
        if (function_has_return_type(method)) {
            method.return_type_ref = substitute_type_ref(method.return_type_ref, substitutions);
        }
        for (ParamDecl& param : method.params) {
            param.type_ref = substitute_type_ref(param.type_ref, substitutions);
        }
    }
    return klass;
}

std::string template_method_name(const Expr& expr, const std::string& callee_base,
                                 size_t method_dot) {
    std::ostringstream out;
    out << trim(callee_base.substr(method_dot + 1)) << "[" << template_args_lookup_text(expr)
        << "]";
    return out.str();
}

bool known_template_constructor_type(const FunctionScope& scope, const std::string& callee) {
    const TypeRef callee_type = parse_type_text(callee);
    const std::string base = base_type(callee_type);
    if (base.find('.') != std::string::npos || base.find("::") != std::string::npos) {
        return scope.symbols.types.contains(base) || scope.symbols.native_classes.contains(base) ||
               class_for_receiver_type(scope.symbols, callee_type) != nullptr;
    }
    return known_type_ref(scope.symbols, callee_type) ||
           class_for_receiver_type(scope.symbols, callee_type) != nullptr;
}

} // namespace dudu
