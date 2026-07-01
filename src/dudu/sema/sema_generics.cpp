#include "dudu/sema/sema_generics.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace dudu {

namespace {

constexpr std::string_view kPackSuffix = "...";

struct GenericTypeBindings {
    std::map<std::string, TypeRef> scalar;
    std::map<std::string, std::vector<TypeRef>> packs;
};

GenericTypeBindings generic_type_bindings(const std::vector<std::string>& params,
                                          const std::vector<TypeRef>& args) {
    GenericTypeBindings out;
    size_t arg_index = 0;
    for (size_t i = 0; i < params.size() && arg_index < args.size(); ++i) {
        const std::string name = generic_param_base_name(params[i]);
        if (generic_param_is_pack(params[i])) {
            out.packs.emplace(name, std::vector<TypeRef>(args.begin() + arg_index, args.end()));
            break;
        }
        out.scalar.emplace(name, args[arg_index]);
        ++arg_index;
    }
    return out;
}

bool generic_param_named(const std::vector<std::string>& params, const std::string& name) {
    return std::any_of(params.begin(), params.end(), [&](const std::string& param) {
        return generic_param_base_name(param) == name;
    });
}

bool unresolved_generic_binding(const TypeRef& binding) {
    return !has_type_ref(binding) || type_ref_is_auto(binding) ||
           type_ref_is_name(binding, "list") || type_ref_is_name(binding, "dict") ||
           type_ref_is_name(binding, "set");
}

bool pack_expansion_name(const TypeRef& type, std::string& out) {
    if (type.kind != TypeKind::PackExpansion || type.children.size() != 1) {
        return false;
    }
    const std::string name = type_ref_head_name(type.children.front());
    if (name.empty()) {
        return false;
    }
    out = name;
    return true;
}

TypeRef substitute_generic_type_ref(const TypeRef& type, const GenericTypeBindings& bindings);

std::vector<TypeRef> substitute_generic_type_ref_list(const std::vector<TypeRef>& types,
                                                      const GenericTypeBindings& bindings) {
    std::vector<TypeRef> out;
    for (const TypeRef& type : types) {
        std::string pack_name;
        if (pack_expansion_name(type, pack_name)) {
            const auto found = bindings.packs.find(pack_name);
            if (found != bindings.packs.end()) {
                out.insert(out.end(), found->second.begin(), found->second.end());
                continue;
            }
        }
        out.push_back(substitute_generic_type_ref(type, bindings));
    }
    return out;
}

TypeRef substitute_generic_type_ref(const TypeRef& type, const GenericTypeBindings& bindings) {
    const std::string key = type_ref_head_name(type);
    if (!key.empty()) {
        if (const auto found = bindings.scalar.find(key); found != bindings.scalar.end()) {
            TypeRef out = found->second;
            out.location = type.location;
            return out;
        }
    }
    TypeRef out = type;
    out.children = substitute_generic_type_ref_list(type.children, bindings);
    if ((out.kind == TypeKind::FixedArray || out.kind == TypeKind::Shaped) &&
        out.children.size() > 1) {
        std::ostringstream value;
        for (size_t i = 1; i < out.children.size(); ++i) {
            if (i > 1) {
                value << ", ";
            }
            value << substitute_type_ref_text(out.children[i], {});
        }
        out.value = value.str();
    }
    return out;
}

bool same_name(std::string_view left, std::string_view right) {
    return trim_copy(std::string(left)) == trim_copy(std::string(right));
}

bool same_generic_param_name(const TypeRef& dim, const std::string& param) {
    const std::string base = generic_param_base_name(param);
    if (same_name(type_ref_head_name(dim), base)) {
        return true;
    }
    if (dim.kind == TypeKind::PackExpansion && dim.children.size() == 1) {
        return same_name(type_ref_head_name(dim.children.front()), base);
    }
    return false;
}

void collect_value_generic_params(const TypeRef& type, const std::vector<std::string>& params,
                                  std::set<std::string>& out) {
    if (type.kind == TypeKind::FixedArray || type.kind == TypeKind::Shaped) {
        std::vector<TypeRef> dims;
        if (type.kind == TypeKind::FixedArray) {
            dims = explicit_array_shape_refs(type);
        } else if (type.children.size() > 1) {
            dims.assign(type.children.begin() + 1, type.children.end());
        }
        for (const TypeRef& dim : dims) {
            for (const std::string& param : params) {
                if (same_generic_param_name(dim, param)) {
                    out.insert(generic_param_base_name(param));
                }
            }
        }
    }
    for (const TypeRef& child : type.children) {
        collect_value_generic_params(child, params, out);
    }
}

void collect_cpp_value_generic_params(const TypeRef& type, const std::vector<std::string>& params,
                                      std::set<std::string>& out) {
    if (type.kind == TypeKind::FixedArray) {
        for (const TypeRef& dim : explicit_array_shape_refs(type)) {
            for (const std::string& param : params) {
                if (same_generic_param_name(dim, param)) {
                    out.insert(generic_param_base_name(param));
                }
            }
        }
    }
    for (const TypeRef& child : type.children) {
        collect_cpp_value_generic_params(child, params, out);
    }
}

bool bind_scalar_generic(const std::string& param, const TypeRef& arg_type,
                         std::map<std::string, TypeRef>& bindings, std::string& error) {
    const auto [it, inserted] = bindings.emplace(param, arg_type);
    if (!inserted && !type_ref_equivalent(it->second, arg_type)) {
        error = "conflicting inferred type argument " + param + ": " +
                substitute_type_ref_text(it->second, {}) + " vs " +
                substitute_type_ref_text(arg_type, {});
        return false;
    }
    return true;
}

bool bind_pack_generic(const std::string& param, const std::vector<TypeRef>& args,
                       std::map<std::string, std::vector<TypeRef>>& bindings, std::string& error) {
    const auto [it, inserted] = bindings.emplace(param, args);
    if (inserted || it->second.size() != args.size()) {
        if (!inserted) {
            error = "conflicting inferred type pack " + param;
            return false;
        }
        return true;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (!type_ref_equivalent(it->second[i], args[i])) {
            error = "conflicting inferred type pack " + param;
            return false;
        }
    }
    return true;
}

bool infer_generic_binding_pack(const TypeRef& param_type, const TypeRef& arg_type,
                                const std::vector<std::string>& params,
                                GenericTypeBindings& bindings, std::string& error) {
    if (arg_type.kind == TypeKind::Shaped && param_type.kind != TypeKind::Shaped &&
        !arg_type.children.empty()) {
        return infer_generic_binding_pack(param_type, arg_type.children.front(), params, bindings,
                                          error);
    }

    const std::string param = type_ref_head_name(param_type);
    if (generic_param_named(params, param)) {
        if (std::any_of(params.begin(), params.end(), [&](const std::string& candidate) {
                return generic_param_is_pack(candidate) &&
                       generic_param_base_name(candidate) == param;
            })) {
            return bind_pack_generic(param, {arg_type}, bindings.packs, error);
        }
        return bind_scalar_generic(param, arg_type, bindings.scalar, error);
    }

    if ((param_type.kind == TypeKind::Pointer || param_type.kind == TypeKind::Reference) &&
        param_type.kind == arg_type.kind && param_type.children.size() == 1 &&
        arg_type.children.size() == 1) {
        return infer_generic_binding_pack(param_type.children.front(), arg_type.children.front(),
                                          params, bindings, error);
    }
    if (param_type.kind != arg_type.kind) {
        return true;
    }
    if ((param_type.kind == TypeKind::FixedArray || param_type.kind == TypeKind::Shaped) &&
        !param_type.children.empty() && !arg_type.children.empty()) {
        if (!infer_generic_binding_pack(param_type.children.front(), arg_type.children.front(),
                                        params, bindings, error)) {
            return false;
        }
        size_t arg_index = 1;
        for (size_t i = 1; i < param_type.children.size(); ++i) {
            std::string pack_name;
            if (pack_expansion_name(param_type.children[i], pack_name)) {
                const size_t fixed_after = param_type.children.size() - i - 1;
                if (arg_type.children.size() < arg_index + fixed_after) {
                    return true;
                }
                const size_t pack_count = arg_type.children.size() - arg_index - fixed_after;
                std::vector<TypeRef> pack_args;
                pack_args.reserve(pack_count);
                for (size_t j = 0; j < pack_count; ++j) {
                    pack_args.push_back(arg_type.children[arg_index + j]);
                }
                if (!bind_pack_generic(pack_name, pack_args, bindings.packs, error)) {
                    return false;
                }
                arg_index += pack_count;
                continue;
            }
            if (arg_index >= arg_type.children.size()) {
                return true;
            }
            if (!infer_generic_binding_pack(param_type.children[i], arg_type.children[arg_index],
                                            params, bindings, error)) {
                return false;
            }
            ++arg_index;
        }
        return true;
    }
    if (param_type.kind == TypeKind::Template) {
        if (trim(param_type.name) != trim(arg_type.name)) {
            return true;
        }
        size_t arg_index = 0;
        for (size_t i = 0; i < param_type.children.size(); ++i) {
            std::string pack_name;
            if (pack_expansion_name(param_type.children[i], pack_name)) {
                const size_t fixed_after = param_type.children.size() - i - 1;
                if (arg_type.children.size() < arg_index + fixed_after) {
                    return true;
                }
                const size_t pack_count = arg_type.children.size() - arg_index - fixed_after;
                std::vector<TypeRef> pack_args;
                pack_args.reserve(pack_count);
                for (size_t j = 0; j < pack_count; ++j) {
                    pack_args.push_back(arg_type.children[arg_index + j]);
                }
                if (!bind_pack_generic(pack_name, pack_args, bindings.packs, error)) {
                    return false;
                }
                arg_index += pack_count;
                continue;
            }
            if (arg_index >= arg_type.children.size()) {
                return true;
            }
            if (!infer_generic_binding_pack(param_type.children[i], arg_type.children[arg_index],
                                            params, bindings, error)) {
                return false;
            }
            ++arg_index;
        }
        return true;
    }

    if (param.empty()) {
        return true;
    }
    if (param_type.children.empty() || param_type.children.size() != arg_type.children.size()) {
        return true;
    }
    for (size_t i = 0; i < param_type.children.size(); ++i) {
        if (!infer_generic_binding_pack(param_type.children[i], arg_type.children[i], params,
                                        bindings, error)) {
            return false;
        }
    }
    return true;
}

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
