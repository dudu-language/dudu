#include "dudu/sema_generics.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_common.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

namespace dudu {

namespace {

std::map<std::string, std::string> generic_substitutions(const std::vector<std::string>& params,
                                                         const std::vector<TypeRef>& args) {
    std::map<std::string, std::string> out;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        out.emplace(params[i], substitute_type_ref_text(args[i], {}));
    }
    return out;
}

bool generic_param_named(const std::vector<std::string>& params, const std::string& name) {
    return std::find(params.begin(), params.end(), name) != params.end();
}

bool infer_generic_binding(const TypeRef& param_type, const TypeRef& arg_type,
                           const std::vector<std::string>& params,
                           std::map<std::string, TypeRef>& bindings, std::string& error) {
    const std::string param = type_ref_head_name(param_type);
    const std::string arg = type_ref_head_name(arg_type);
    if (generic_param_named(params, param)) {
        const std::string arg_text = substitute_type_ref_text(arg_type, {});
        const auto [it, inserted] = bindings.emplace(param, arg_type);
        if (!inserted && substitute_type_ref_text(it->second, {}) != arg_text) {
            error = "conflicting inferred type argument " + param + ": " +
                    substitute_type_ref_text(it->second, {}) + " vs " + arg_text;
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

TypeRef infer_callback_expr_type(const GenericInferCallbacks& callbacks, const FunctionScope& scope,
                                 const Expr& expr, const SourceLocation* location) {
    return callbacks.infer_expr_type(scope, expr, location);
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

std::optional<std::vector<TypeRef>>
infer_generic_call_type_args(const FunctionScope& scope, const FunctionDecl& fn,
                             const std::string& callee, const std::vector<Expr>& args,
                             const SourceLocation* location,
                             const GenericInferCallbacks& callbacks) {
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
        const TypeRef got = infer_callback_expr_type(callbacks, scope, args[i], location);
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
        const std::string binding_text = binding == bindings.end()
                                             ? std::string{}
                                             : substitute_type_ref_text(binding->second, {});
        if (binding == bindings.end() || binding_text.empty() || binding_text == "auto" ||
            binding_text == "list" || binding_text == "dict" || binding_text == "set") {
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
                               size_t first_param, const SourceLocation* location,
                               const GenericInferCallbacks& callbacks) {
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
        arg_types.push_back(infer_callback_expr_type(callbacks, scope, args[i], location));
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
    if (expected_return && has_type_ref(*expected_return) &&
        method.return_type_ref.kind != TypeKind::Unknown) {
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
        const std::string binding_text = binding == bindings.end()
                                             ? std::string{}
                                             : substitute_type_ref_text(binding->second, {});
        if (binding == bindings.end() || binding_text.empty() || binding_text == "auto" ||
            binding_text == "list" || binding_text == "dict" || binding_text == "set") {
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
    const std::map<std::string, std::string> substitutions =
        generic_substitutions(fn.generic_params, args);
    FunctionSignature signature;
    signature.return_type_ref = fn.return_type.empty()
                                    ? parse_type_text("void", fn.location)
                                    : substitute_type_ref(fn.return_type_ref, substitutions);
    signature.return_type = substitute_type_ref_text(signature.return_type_ref, {});
    for (const ParamDecl& param : fn.params) {
        TypeRef param_type = substitute_type_ref(param.type_ref, substitutions);
        signature.params.push_back(substitute_type_ref_text(param_type, {}));
        signature.param_type_refs.push_back(std::move(param_type));
    }
    return signature;
}

ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name) {
    const std::map<std::string, std::string> substitutions =
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
        if (!method.return_type.empty()) {
            method.return_type_ref = substitute_type_ref(method.return_type_ref, substitutions);
            method.return_type = substitute_type_ref_text(method.return_type_ref, {});
        }
        for (ParamDecl& param : method.params) {
            param.type_ref = substitute_type_ref(param.type_ref, substitutions);
        }
    }
    return klass;
}

std::string join_type_ref_texts(const std::vector<TypeRef>& types) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(types[i], {});
    }
    return out.str();
}

std::string template_method_name(const Expr& expr, const std::string& callee_base,
                                 size_t method_dot) {
    std::ostringstream out;
    out << trim(callee_base.substr(method_dot + 1)) << "[" << template_args_lookup_text(expr)
        << "]";
    return out.str();
}

bool known_template_constructor_type(const FunctionScope& scope, const std::string& callee) {
    const std::string base = base_type(callee);
    if (base.find('.') != std::string::npos || base.find("::") != std::string::npos) {
        return scope.symbols.types.contains(base) || scope.symbols.native_classes.contains(base) ||
               scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
    }
    return known_type(scope.symbols, callee) ||
           scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
}

} // namespace dudu
