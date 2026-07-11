#include "dudu/sema/sema_generics.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics_detail.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace dudu {

namespace {

constexpr std::string_view kPackSuffix = "...";

} // namespace

bool generic_param_is_pack(std::string_view param) {
    return param.size() > kPackSuffix.size() &&
           param.substr(param.size() - kPackSuffix.size()) == kPackSuffix;
}

std::string generic_param_base_name(std::string_view param) {
    if (generic_param_is_pack(param)) {
        param.remove_suffix(kPackSuffix.size());
    }
    return std::string(param);
}

bool generic_pack_param_named(const std::vector<std::string>& params, std::string_view name) {
    return std::any_of(params.begin(), params.end(), [&](const std::string& param) {
        return generic_param_is_pack(param) && generic_param_base_name(param) == name;
    });
}

bool generic_arity_matches(const std::vector<std::string>& params, size_t arg_count) {
    if (params.empty()) {
        return arg_count == 0;
    }
    if (generic_param_is_pack(params.back())) {
        return arg_count >= params.size() - 1;
    }
    return arg_count == params.size();
}

size_t generic_min_arity(const std::vector<std::string>& params) {
    return !params.empty() && generic_param_is_pack(params.back()) ? params.size() - 1
                                                                   : params.size();
}

size_t generic_decl_min_arity(const std::vector<std::string>& params,
                              std::optional<size_t> minimum) {
    return minimum.value_or(generic_min_arity(params));
}

bool generic_decl_arity_matches(const std::vector<std::string>& params,
                                std::optional<size_t> minimum, size_t arg_count) {
    if (arg_count < generic_decl_min_arity(params, minimum)) {
        return false;
    }
    if (!params.empty() && generic_param_is_pack(params.back())) {
        return true;
    }
    return arg_count <= params.size();
}

size_t class_generic_min_arity(const ClassDecl& klass) {
    return generic_decl_min_arity(klass.generic_params, klass.generic_min_args);
}

bool class_generic_arity_matches(const ClassDecl& klass, size_t arg_count) {
    return generic_decl_arity_matches(klass.generic_params, klass.generic_min_args, arg_count);
}

TypeRef substitute_generic_type_ref(const std::vector<std::string>& params,
                                    const std::vector<TypeRef>& args, const TypeRef& type) {
    return substitute_generic_type_ref(type, generic_type_bindings(params, args));
}

std::vector<TypeRef> template_type_refs(const Expr& expr) {
    if (has_expr_template_type_args(expr)) {
        return expr_template_type_args(expr);
    }
    if (has_expr_template_args(expr)) {
        sema_fail(expr.location, "malformed template call: missing parsed type arguments");
    }
    return {};
}

std::set<std::string> generic_value_params(const std::vector<std::string>& params,
                                           const std::vector<TypeRef>& type_refs) {
    std::set<std::string> out;
    for (const TypeRef& type : type_refs) {
        collect_value_generic_params(type, params, out);
    }
    return out;
}

std::set<std::string> generic_cpp_value_params(const std::vector<std::string>& params,
                                               const std::vector<TypeRef>& type_refs) {
    std::set<std::string> out;
    for (const TypeRef& type : type_refs) {
        collect_cpp_value_generic_params(type, params, out);
    }
    return out;
}

std::vector<std::string> generic_cpp_params(const std::vector<std::string>& params,
                                            const std::set<std::string>& semantic_value_params,
                                            const std::set<std::string>& cpp_value_params) {
    std::vector<std::string> out;
    out.reserve(params.size());
    for (const std::string& param : params) {
        const std::string base = generic_param_base_name(param);
        if (!semantic_value_params.contains(base) || cpp_value_params.contains(base)) {
            out.push_back(param);
        }
    }
    return out;
}

std::set<std::string> generic_value_params_for_function(const FunctionDecl& fn) {
    std::vector<TypeRef> refs;
    refs.reserve(fn.params.size() + 1);
    if (function_has_return_type(fn)) {
        refs.push_back(fn.return_type_ref);
    }
    for (const ParamDecl& param : fn.params) {
        refs.push_back(param.type_ref);
    }
    return generic_value_params(fn.generic_params, refs);
}

std::set<std::string> generic_cpp_value_params_for_function(const FunctionDecl& fn) {
    std::vector<TypeRef> refs;
    refs.reserve(fn.params.size() + 1);
    if (function_has_return_type(fn)) {
        refs.push_back(fn.return_type_ref);
    }
    for (const ParamDecl& param : fn.params) {
        refs.push_back(param.type_ref);
    }
    return generic_cpp_value_params(fn.generic_params, refs);
}

std::vector<std::string> generic_cpp_params_for_function(const FunctionDecl& fn) {
    const std::set<std::string> value_params = generic_value_params_for_function(fn);
    const std::set<std::string> cpp_value_params = generic_cpp_value_params_for_function(fn);
    return generic_cpp_params(fn.generic_params, value_params, cpp_value_params);
}

std::set<std::string> generic_value_params_for_class(const ClassDecl& klass) {
    std::vector<TypeRef> refs;
    for (const BaseClassDecl& base : klass.base_class_refs) {
        refs.push_back(base.type_ref);
    }
    for (const FieldDecl& field : klass.fields) {
        refs.push_back(field.type_ref);
    }
    for (const ConstDecl& field : klass.static_fields) {
        refs.push_back(field.type_ref);
    }
    for (const ConstDecl& constant : klass.constants) {
        refs.push_back(constant.type_ref);
    }
    for (const FunctionDecl& method : klass.methods) {
        if (function_has_return_type(method)) {
            refs.push_back(method.return_type_ref);
        }
        for (const ParamDecl& param : method.params) {
            refs.push_back(param.type_ref);
        }
    }
    return generic_value_params(klass.generic_params, refs);
}

std::set<std::string> generic_cpp_value_params_for_class(const ClassDecl& klass) {
    std::vector<TypeRef> refs;
    for (const BaseClassDecl& base : klass.base_class_refs) {
        refs.push_back(base.type_ref);
    }
    for (const FieldDecl& field : klass.fields) {
        refs.push_back(field.type_ref);
    }
    for (const ConstDecl& field : klass.static_fields) {
        refs.push_back(field.type_ref);
    }
    for (const ConstDecl& constant : klass.constants) {
        refs.push_back(constant.type_ref);
    }
    for (const FunctionDecl& method : klass.methods) {
        if (function_has_return_type(method)) {
            refs.push_back(method.return_type_ref);
        }
        for (const ParamDecl& param : method.params) {
            refs.push_back(param.type_ref);
        }
    }
    return generic_cpp_value_params(klass.generic_params, refs);
}

std::vector<std::string> generic_cpp_params_for_class(const ClassDecl& klass) {
    const std::set<std::string> value_params = generic_value_params_for_class(klass);
    const std::set<std::string> cpp_value_params = generic_cpp_value_params_for_class(klass);
    return generic_cpp_params(klass.generic_params, value_params, cpp_value_params);
}

std::optional<std::vector<TypeRef>> infer_generic_call_type_args(const FunctionScope& scope,
                                                                 const FunctionDecl& fn,
                                                                 const std::string& callee,
                                                                 const std::vector<Expr>& args,
                                                                 const SourceLocation* location) {
    if (fn.generic_params.empty()) {
        return std::nullopt;
    }
    FunctionSignature shape;
    std::vector<TypeRef> param_types;
    param_types.reserve(fn.params.size());
    for (size_t i = 0; i < fn.params.size(); ++i) {
        param_types.push_back(fn.params[i].type_ref);
        if (fn.params[i].variadic) {
            shape.variadic = true;
            shape.variadic_param_index = static_cast<int>(i);
        }
    }
    set_signature_param_types(shape, std::move(param_types));
    const size_t min_args =
        shape.variadic ? signature_param_count(shape) - 1 : signature_param_count(shape);
    if (location != nullptr && ((!shape.variadic && args.size() != signature_param_count(shape)) ||
                                (shape.variadic && args.size() < min_args))) {
        sema_fail(*location, "function " + callee + " expects " +
                                 std::to_string(signature_param_count(shape)) + " arguments, got " +
                                 std::to_string(args.size()));
    }
    if ((!shape.variadic && args.size() != signature_param_count(shape)) ||
        (shape.variadic && args.size() < min_args)) {
        return std::nullopt;
    }
    GenericTypeBindings bindings;
    std::map<size_t, std::vector<TypeRef>> variadic_args;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (fn.params[i].variadic) {
            variadic_args[i] = {};
        }
    }
    for (size_t i = 0; i < args.size(); ++i) {
        const TypeRef got = infer_expr_type_ast(scope, args[i], location);
        const size_t param_index = signature_param_index_for_arg(shape, i, args.size());
        if (fn.params[param_index].variadic) {
            variadic_args[param_index].push_back(got);
            continue;
        }
        std::string error;
        if (!infer_generic_binding_pack(fn.params[param_index].type_ref, got, fn.generic_params,
                                        bindings, error)) {
            if (location != nullptr) {
                sema_fail(diagnostic_location(*location, args[i]), error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    for (const auto& [param_index, pack_args] : variadic_args) {
        const std::string name = type_ref_head_name(fn.params[param_index].type_ref);
        if (!name.empty()) {
            std::string error;
            if (!bind_pack_generic(name, pack_args, bindings.packs, error)) {
                if (location != nullptr) {
                    sema_fail(*location, error + " for " + callee);
                }
                return std::nullopt;
            }
        }
    }
    std::vector<TypeRef> out;
    out.reserve(fn.generic_params.size());
    for (const std::string& param : fn.generic_params) {
        const std::string base = generic_param_base_name(param);
        if (generic_param_is_pack(param)) {
            const auto pack = bindings.packs.find(base);
            if (pack == bindings.packs.end()) {
                if (location != nullptr) {
                    sema_fail(*location, "cannot infer type argument " + param + " for " + callee);
                }
                return std::nullopt;
            }
            out.insert(out.end(), pack->second.begin(), pack->second.end());
            continue;
        }
        const auto binding = bindings.scalar.find(base);
        if (binding == bindings.scalar.end() || unresolved_generic_binding(binding->second)) {
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
                               const TypeRef* receiver_type) {
    if (method.generic_params.empty()) {
        return std::nullopt;
    }
    const size_t expected_args =
        method.params.size() >= first_param ? method.params.size() - first_param : 0;
    bool variadic = false;
    size_t min_args = expected_args;
    for (size_t i = first_param; i < method.params.size(); ++i) {
        if (method.params[i].variadic) {
            variadic = true;
            min_args = expected_args - 1;
            break;
        }
    }
    if (location != nullptr &&
        ((!variadic && args.size() != expected_args) || (variadic && args.size() < min_args))) {
        sema_fail(*location, "method " + callee + " expects " + std::to_string(expected_args) +
                                 " arguments, got " + std::to_string(args.size()));
    }
    if ((!variadic && args.size() != expected_args) || (variadic && args.size() < min_args)) {
        return std::nullopt;
    }
    std::vector<TypeRef> arg_types;
    arg_types.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        arg_types.push_back(infer_expr_type_ast(scope, args[i], location));
    }
    return infer_generic_method_type_args_from_type_refs(method, callee, arg_types, first_param,
                                                         std::nullopt, location, receiver_type);
}

std::optional<std::vector<TypeRef>> infer_generic_method_type_args_from_type_refs(
    const FunctionDecl& method, const std::string& callee, const std::vector<TypeRef>& arg_types,
    size_t first_param, const std::optional<TypeRef>& expected_return,
    const SourceLocation* location, const TypeRef* receiver_type) {
    if (method.generic_params.empty()) {
        return std::nullopt;
    }
    const size_t expected_args =
        method.params.size() >= first_param ? method.params.size() - first_param : 0;
    FunctionSignature shape;
    std::vector<TypeRef> param_types;
    param_types.reserve(expected_args);
    for (size_t i = first_param; i < method.params.size(); ++i) {
        param_types.push_back(method.params[i].type_ref);
        if (method.params[i].variadic) {
            shape.variadic = true;
            shape.variadic_param_index = static_cast<int>(i - first_param);
        }
    }
    set_signature_param_types(shape, std::move(param_types));
    const size_t min_args =
        shape.variadic ? signature_param_count(shape) - 1 : signature_param_count(shape);
    if (location != nullptr &&
        ((!shape.variadic && arg_types.size() != signature_param_count(shape)) ||
         (shape.variadic && arg_types.size() < min_args))) {
        sema_fail(*location, "method " + callee + " expects " + std::to_string(expected_args) +
                                 " arguments, got " + std::to_string(arg_types.size()));
    }
    if ((!shape.variadic && arg_types.size() != signature_param_count(shape)) ||
        (shape.variadic && arg_types.size() < min_args)) {
        return std::nullopt;
    }
    GenericTypeBindings bindings;
    if (receiver_type != nullptr && first_param > 0 && !method.params.empty() &&
        method.params.front().name == "self") {
        TypeRef receiver_arg = *receiver_type;
        const TypeRef& self_param = method.params.front().type_ref;
        if (self_param.kind == TypeKind::Reference && self_param.children.size() == 1) {
            TypeRef receiver_inner = *receiver_type;
            if (self_param.children.front().kind == TypeKind::Const) {
                receiver_inner = wrapped_type_ref(TypeKind::Const, std::move(receiver_inner),
                                                  self_param.location);
            }
            receiver_arg = wrapped_type_ref(TypeKind::Reference, std::move(receiver_inner),
                                            self_param.location);
        }
        std::string error;
        if (!infer_generic_binding_pack(self_param, receiver_arg, method.generic_params, bindings,
                                        error)) {
            if (location != nullptr) {
                sema_fail(*location, error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    std::map<size_t, std::vector<TypeRef>> variadic_args;
    for (size_t i = first_param; i < method.params.size(); ++i) {
        if (method.params[i].variadic) {
            variadic_args[i] = {};
        }
    }
    for (size_t i = 0; i < arg_types.size(); ++i) {
        const size_t param_offset = signature_param_index_for_arg(shape, i, arg_types.size());
        const size_t param_index = first_param + param_offset;
        if (method.params[param_index].variadic) {
            variadic_args[param_index].push_back(arg_types[i]);
            continue;
        }
        std::string error;
        if (!infer_generic_binding_pack(method.params[param_index].type_ref, arg_types[i],
                                        method.generic_params, bindings, error)) {
            if (location != nullptr) {
                sema_fail(*location, error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    for (const auto& [param_index, pack_args] : variadic_args) {
        const std::string name = type_ref_head_name(method.params[param_index].type_ref);
        if (!name.empty()) {
            std::string error;
            if (!bind_pack_generic(name, pack_args, bindings.packs, error)) {
                if (location != nullptr) {
                    sema_fail(*location, error + " for " + callee);
                }
                return std::nullopt;
            }
        }
    }
    if (expected_return && has_type_ref(*expected_return) && has_type_ref(method.return_type_ref)) {
        std::string error;
        if (!infer_generic_binding_pack(method.return_type_ref, *expected_return,
                                        method.generic_params, bindings, error)) {
            if (location != nullptr) {
                sema_fail(*location, error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    std::vector<TypeRef> out;
    out.reserve(method.generic_params.size());
    for (const std::string& param : method.generic_params) {
        const std::string base = generic_param_base_name(param);
        if (generic_param_is_pack(param)) {
            const auto pack = bindings.packs.find(base);
            if (pack == bindings.packs.end()) {
                if (location != nullptr) {
                    sema_fail(*location, "cannot infer type argument " + param + " for " + callee);
                }
                return std::nullopt;
            }
            out.insert(out.end(), pack->second.begin(), pack->second.end());
            continue;
        }
        const auto binding = bindings.scalar.find(base);
        if (binding == bindings.scalar.end() || unresolved_generic_binding(binding->second)) {
            if (location != nullptr) {
                sema_fail(*location, "cannot infer type argument " + param + " for " + callee);
            }
            return std::nullopt;
        }
        out.push_back(binding->second);
    }
    return out;
}

} // namespace dudu
