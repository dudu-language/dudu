#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/shape_value_expr.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_generics_detail.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>

namespace dudu {
namespace {

bool generic_param_named(const std::vector<std::string>& params, const std::string& name) {
    return std::any_of(params.begin(), params.end(), [&](const std::string& param) {
        return generic_param_base_name(param) == name;
    });
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

std::map<std::string, std::string>
scalar_binding_texts(const std::map<std::string, TypeRef>& bindings) {
    std::map<std::string, std::string> out;
    for (const auto& [name, type] : bindings) {
        out.emplace(name, substitute_type_ref_text(type, {}));
    }
    return out;
}

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

bool same_name(std::string_view left, std::string_view right) {
    return trim_copy(std::string(left)) == trim_copy(std::string(right));
}

bool same_generic_param_name(const TypeRef& dim, const std::string& param) {
    const std::string base = generic_param_base_name(param);
    if (same_name(type_ref_head_name(dim), base)) {
        return true;
    }
    if (dim.kind == TypeKind::Value) {
        const std::set<std::string> identifiers = shape_value_expr_identifiers(dim.value);
        if (identifiers.contains(base)) {
            return true;
        }
    }
    if (dim.kind == TypeKind::PackExpansion && dim.children.size() == 1) {
        return same_name(type_ref_head_name(dim.children.front()), base);
    }
    return false;
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

} // namespace

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

bool unresolved_generic_binding(const TypeRef& binding) {
    return !has_type_ref(binding) || type_ref_is_auto(binding) ||
           type_ref_is_name(binding, "list") || type_ref_is_name(binding, "dict") ||
           type_ref_is_name(binding, "set");
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
    if (type.kind == TypeKind::Value) {
        TypeRef out = type;
        out.value = shape_value_expr_substitute(type.value, scalar_binding_texts(bindings.scalar));
        return out;
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

} // namespace dudu
