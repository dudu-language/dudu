#include "dudu/native/native_signature_substitution.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics_detail.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <algorithm>
#include <cctype>

namespace dudu {
namespace {

std::string replace_type_identifier(std::string type, const std::string& name,
                                    const std::string& arg) {
    if (name.empty() || arg.empty()) {
        return type;
    }
    size_t pos = type.find(name);
    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(type[pos - 1])) == 0 && type[pos - 1] != '_');
        const size_t end = pos + name.size();
        const bool right_ok =
            end >= type.size() ||
            (std::isalnum(static_cast<unsigned char>(type[end])) == 0 && type[end] != '_');
        if (left_ok && right_ok) {
            type.replace(pos, name.size(), arg);
            pos = type.find(name, pos + arg.size());
        } else {
            pos = type.find(name, pos + 1);
        }
    }
    return type;
}

std::string join_native_type_ref_spellings(const std::vector<TypeRef>& types) {
    std::string out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += substitute_type_ref_text(types[i], {});
    }
    return out;
}

std::string replace_all(std::string text, const std::string& needle, const std::string& value) {
    size_t pos = text.find(needle);
    while (pos != std::string::npos) {
        text.replace(pos, needle.size(), value);
        pos = text.find(needle, pos + value.size());
    }
    return text;
}

std::string replace_native_template_spelling_bindings(std::string type,
                                                      const NativeTemplateBindings& bindings,
                                                      const NativePackBindingMap& pack_bindings) {
    for (const auto& [name, args] : pack_bindings) {
        type = replace_all(std::move(type), name + "...", join_native_type_ref_spellings(args));
    }
    for (const auto& [name, arg] : bindings) {
        type = replace_type_identifier(std::move(type), name, substitute_type_ref_text(arg, {}));
    }
    return type;
}

TypeRef substitute_native_spelling_type_ref(const TypeRef& type,
                                            const NativeTemplateBindings& bindings,
                                            const NativePackBindingMap& pack_bindings) {
    const std::string native_spelling = type_ref_text(type);
    return parse_type_text(
        replace_native_template_spelling_bindings(native_spelling, bindings, pack_bindings),
        type.location);
}

TypeRef require_signature_type_ref(const TypeRef& type, std::string_view context) {
    if (has_type_ref(type)) {
        return type;
    }
    throw CompileError(type.location, "malformed native signature: missing " +
                                          std::string(context) + " type metadata");
}

bool structured_binding_type_ref(const TypeRef& type) {
    if (!has_type_ref(type)) {
        return false;
    }
    if (type.kind == TypeKind::Unknown) {
        return false;
    }
    if (type.kind == TypeKind::PackExpansion) {
        return type.children.size() == 1 && structured_binding_type_ref(type.children.front());
    }
    for (const TypeRef& child : type.children) {
        if (!structured_binding_type_ref(child)) {
            return false;
        }
    }
    return true;
}

std::map<std::string, TypeRef>
structured_type_ref_bindings(const NativeTemplateBindings& bindings) {
    std::map<std::string, TypeRef> out;
    for (const auto& [name, type] : bindings) {
        if (structured_binding_type_ref(type)) {
            out.emplace(name, type);
        }
    }
    return out;
}

bool structured_substitution_allowed(const TypeRef& type, const NativeTemplateBindings& bindings) {
    (void)bindings;
    if (!structured_binding_type_ref(type)) {
        return false;
    }
    if (type_ref_contains_kind(type, TypeKind::PackExpansion)) {
        return false;
    }
    return true;
}

std::optional<std::string>
bound_pack_placeholder(const TypeRef& type, const NativePackBindingMap& pack_bindings) {
    if (type.kind == TypeKind::PackExpansion && type.children.size() == 1) {
        return bound_pack_placeholder(type.children.front(), pack_bindings);
    }
    const std::string name = type_ref_head_name(type);
    if (pack_bindings.contains(name)) {
        return name;
    }
    for (const TypeRef& child : type.children) {
        if (const std::optional<std::string> found =
                bound_pack_placeholder(child, pack_bindings)) {
            return found;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<TypeRef>>
structured_pack_expansion(const TypeRef& param_type, const NativePackBindingMap& pack_bindings) {
    if (param_type.kind != TypeKind::PackExpansion || param_type.children.size() != 1) {
        return std::nullopt;
    }
    const std::optional<std::string> pack_name =
        bound_pack_placeholder(param_type.children.front(), pack_bindings);
    if (!pack_name) {
        return std::nullopt;
    }
    const auto found = pack_bindings.find(*pack_name);
    if (found == pack_bindings.end()) {
        return std::nullopt;
    }
    std::vector<TypeRef> expanded;
    expanded.reserve(found->second.size());
    for (const TypeRef& binding : found->second) {
        expanded.push_back(substitute_type_ref(
            param_type.children.front(), {{*pack_name, binding}}));
    }
    return expanded;
}

TypeRef expand_nested_pack_expansions(TypeRef type, const NativePackBindingMap& pack_bindings,
                                      bool& changed) {
    std::vector<TypeRef> children;
    children.reserve(type.children.size());
    for (TypeRef child : type.children) {
        if (const std::optional<std::vector<TypeRef>> expanded =
                structured_pack_expansion(child, pack_bindings)) {
            children.insert(children.end(), expanded->begin(), expanded->end());
            changed = true;
            continue;
        }
        children.push_back(expand_nested_pack_expansions(std::move(child), pack_bindings, changed));
    }
    type.children = std::move(children);
    return type;
}

} // namespace

FunctionSignature substitute_explicit_template_signature(const Symbols& symbols,
                                                         FunctionSignature signature,
                                                         const std::vector<TypeRef>& args) {
    const GenericTypeBindings bindings =
        generic_type_bindings(signature.template_params, args);
    for (TypeRef& default_arg : signature.template_default_args) {
        if (has_type_ref(default_arg)) {
            default_arg = substitute_generic_type_ref(default_arg, bindings);
        }
    }
    std::vector<TypeRef> param_types;
    param_types.reserve(signature_param_count(signature));
    for (size_t i = 0; i < signature_param_count(signature); ++i) {
        const TypeRef param_type =
            require_signature_type_ref(signature_param_type_ref(signature, i), "parameter");
        param_types.push_back(resolve_associated_type_ref(
            symbols,
            normalize_cpp_type_artifacts_ref(
                substitute_generic_type_ref(param_type, bindings))));
    }
    set_signature_param_types(signature, std::move(param_types));
    const TypeRef return_type =
        require_signature_type_ref(signature_return_type_ref(signature), "return");
    set_signature_return_type(
        signature,
        resolve_associated_type_ref(symbols, normalize_cpp_type_artifacts_ref(
                                                 substitute_generic_type_ref(return_type,
                                                                             bindings))));
    return signature;
}

FunctionSignature substitute_bound_template_signature(const Symbols& symbols,
                                                      FunctionSignature signature,
                                                      const NativeTemplateBindings& bindings,
                                                      const NativePackBindingMap& pack_bindings) {
    const std::map<std::string, TypeRef> refs = structured_type_ref_bindings(bindings);

    std::vector<TypeRef> params;
    params.reserve(signature_param_count(signature));
    for (size_t i = 0; i < signature_param_count(signature); ++i) {
        const TypeRef param_type = signature_param_type_ref(signature, i);
        if (param_type.kind == TypeKind::PackExpansion && param_type.children.empty()) {
            params.push_back(param_type);
            continue;
        }
        if (const std::optional<std::vector<TypeRef>> expanded =
                structured_pack_expansion(param_type, pack_bindings)) {
            params.insert(params.end(), expanded->begin(), expanded->end());
            continue;
        }
        if (structured_substitution_allowed(param_type, bindings)) {
            params.push_back(resolve_associated_type_ref(
                symbols, normalize_cpp_type_artifacts_ref(substitute_type_ref(param_type, refs))));
            continue;
        }
        params.push_back(substitute_native_spelling_type_ref(param_type, bindings, pack_bindings));
    }
    set_signature_param_types(signature, std::move(params));

    const TypeRef return_type = signature_return_type_ref(signature);
    bool expanded_return_pack = false;
    TypeRef expanded_return =
        expand_nested_pack_expansions(return_type, pack_bindings, expanded_return_pack);
    if (expanded_return_pack) {
        if (structured_substitution_allowed(expanded_return, bindings)) {
            expanded_return = resolve_associated_type_ref(
                symbols,
                normalize_cpp_type_artifacts_ref(substitute_type_ref(expanded_return, refs)));
        }
        set_signature_return_type(signature, std::move(expanded_return));
        return signature;
    }
    if (structured_substitution_allowed(return_type, bindings)) {
        set_signature_return_type(
            signature,
            resolve_associated_type_ref(
                symbols, normalize_cpp_type_artifacts_ref(substitute_type_ref(return_type, refs))));
        return signature;
    }
    set_signature_return_type(
        signature, substitute_native_spelling_type_ref(return_type, bindings, pack_bindings));
    return signature;
}

} // namespace dudu
