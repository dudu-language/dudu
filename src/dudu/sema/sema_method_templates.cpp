#include "dudu/sema/sema_method_templates.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace dudu {
namespace {

std::map<std::string, TypeRef>
receiver_template_ref_substitutions(const ClassDecl& klass,
                                    const std::vector<TypeRef>& receiver_args) {
    std::map<std::string, TypeRef> substitutions;
    const std::vector<TypeRef> concrete_args =
        generic_args_with_defaults(klass.generic_params, klass.generic_default_args, receiver_args);
    for (size_t i = 0; i < klass.generic_params.size() && i < concrete_args.size(); ++i) {
        substitutions.insert_or_assign(generic_param_base_name(klass.generic_params[i]),
                                       concrete_args[i]);
    }
    for (size_t pass = 0; pass <= klass.type_aliases.size(); ++pass) {
        for (const TypeAliasDecl& alias : klass.type_aliases) {
            TypeRef resolved = substitute_type_ref(alias.type_ref, substitutions);
            substitutions.insert_or_assign(alias.name, resolved);
            substitutions.insert_or_assign(klass.name + "." + alias.name, resolved);
            substitutions.insert_or_assign(klass.name + "::" + alias.name, std::move(resolved));
        }
    }
    return substitutions;
}

std::optional<std::pair<std::string, std::string>> associated_type_path(std::string_view head) {
    const size_t dot = head.rfind('.');
    const size_t scope = head.rfind("::");
    if (dot == std::string_view::npos && scope == std::string_view::npos) {
        return std::nullopt;
    }
    const bool use_scope =
        scope != std::string_view::npos && (dot == std::string_view::npos || scope > dot);
    const size_t separator = use_scope ? scope : dot;
    const size_t width = use_scope ? 2 : 1;
    if (separator == 0 || separator + width >= head.size()) {
        return std::nullopt;
    }
    return std::pair{std::string(head.substr(0, separator)),
                     std::string(head.substr(separator + width))};
}

bool specialization_values_equivalent(std::string_view pattern, std::string_view actual) {
    if (pattern == actual) {
        return true;
    }
    return (pattern == "1" && actual == "true") || (pattern == "true" && actual == "1") ||
           (pattern == "0" && actual == "false") || (pattern == "false" && actual == "0");
}

bool bind_specialization_pattern(const TypeRef& pattern, const TypeRef& actual,
                                 const std::set<std::string>& params,
                                 std::map<std::string, TypeRef>& bindings, int& score) {
    const std::string pattern_name = type_ref_head_name(pattern);
    if ((pattern.kind == TypeKind::Named || pattern.kind == TypeKind::Qualified) &&
        params.contains(pattern_name)) {
        const auto found = bindings.find(pattern_name);
        if (found == bindings.end()) {
            bindings.emplace(pattern_name, actual);
            return true;
        }
        return type_ref_equivalent(found->second, actual) ||
               type_ref_text(found->second) == type_ref_text(actual);
    }
    if (pattern.kind == TypeKind::Value) {
        ++score;
        return actual.kind != TypeKind::Value ||
               specialization_values_equivalent(pattern.value, actual.value);
    }
    if (pattern.kind != actual.kind || pattern.children.size() != actual.children.size()) {
        return false;
    }
    if ((pattern.kind == TypeKind::Named || pattern.kind == TypeKind::Qualified ||
         pattern.kind == TypeKind::Template || pattern.kind == TypeKind::Associated) &&
        type_ref_head_name(pattern) != type_ref_head_name(actual)) {
        return false;
    }
    if (pattern.kind == TypeKind::Named || pattern.kind == TypeKind::Qualified ||
        pattern.kind == TypeKind::Template || pattern.kind == TypeKind::Associated) {
        ++score;
    }
    for (size_t i = 0; i < pattern.children.size(); ++i) {
        if (!bind_specialization_pattern(pattern.children[i], actual.children[i], params, bindings,
                                         score)) {
            return false;
        }
    }
    return true;
}

const ClassDecl* specialized_class_for_owner(const Symbols& symbols, const TypeRef& owner,
                                             std::string_view alias_name,
                                             std::map<std::string, TypeRef>& bindings) {
    const std::string owner_name = receiver_class_name(symbols, owner);
    const auto found = symbols.native_class_specializations.find(owner_name);
    if (found == symbols.native_class_specializations.end()) {
        return nullptr;
    }
    std::vector<TypeRef> actual_args = template_arg_refs_from_type(owner);
    if (const ClassDecl* primary = class_for_receiver_type(symbols, owner);
        primary != nullptr && !primary->generic_params.empty()) {
        actual_args = generic_args_with_defaults(primary->generic_params,
                                                 primary->generic_default_args, actual_args);
    }
    const ClassDecl* selected = nullptr;
    int selected_score = -1;
    bool ambiguous = false;
    std::map<std::string, TypeRef> selected_bindings;
    for (const ClassDecl& candidate : found->second) {
        if (candidate.native_specialization_args.size() != actual_args.size() ||
            std::ranges::none_of(candidate.type_aliases, [&](const TypeAliasDecl& alias) {
                return alias.name == alias_name;
            })) {
            continue;
        }
        std::set<std::string> params;
        for (const std::string& param : candidate.generic_params) {
            params.insert(generic_param_base_name(param));
        }
        std::map<std::string, TypeRef> candidate_bindings;
        int score = candidate.native_partial_specialization ? 0 : 1000;
        bool matches = true;
        for (size_t i = 0; i < actual_args.size(); ++i) {
            if (!bind_specialization_pattern(candidate.native_specialization_args[i],
                                             actual_args[i], params, candidate_bindings, score)) {
                matches = false;
                break;
            }
        }
        if (!matches || score < selected_score) {
            continue;
        }
        if (score == selected_score) {
            ambiguous = true;
            continue;
        }
        selected = &candidate;
        selected_score = score;
        selected_bindings = std::move(candidate_bindings);
        ambiguous = false;
    }
    if (ambiguous) {
        return nullptr;
    }
    bindings = std::move(selected_bindings);
    return selected;
}

TypeRef resolve_associated_type_ref_impl(TypeRef type, const Symbols& symbols,
                                         const std::map<std::string, TypeRef>& substitutions,
                                         size_t depth) {
    type = resolve_alias_ref(symbols, substitute_type_ref(type, substitutions));
    if (depth > 16) {
        return type;
    }
    for (TypeRef& child : type.children) {
        child =
            resolve_associated_type_ref_impl(std::move(child), symbols, substitutions, depth + 1);
    }
    if (type.kind == TypeKind::Associated && type.children.size() == 1) {
        TypeRef owner = receiver_template_type_ref(symbols, type.children.front());
        std::map<std::string, TypeRef> specialization_bindings;
        const ClassDecl* owner_class =
            specialized_class_for_owner(symbols, owner, type.name, specialization_bindings);
        if (owner_class == nullptr) {
            owner_class = class_for_receiver_type(symbols, owner);
        }
        if (owner_class == nullptr) {
            return type;
        }
        for (const TypeAliasDecl& alias : owner_class->type_aliases) {
            if (alias.name != type.name) {
                continue;
            }
            TypeRef resolved;
            if (!specialization_bindings.empty()) {
                resolved = substitute_type_ref(alias.type_ref, specialization_bindings);
                std::map<std::string, TypeRef> nested_substitutions = substitutions;
                nested_substitutions.insert(specialization_bindings.begin(),
                                            specialization_bindings.end());
                return resolve_associated_type_ref_impl(std::move(resolved), symbols,
                                                        nested_substitutions, depth + 1);
            } else {
                const std::vector<TypeRef> owner_args = template_arg_refs_from_type(owner);
                resolved =
                    substitute_receiver_template_type(alias.type_ref, *owner_class, owner_args);
            }
            return resolve_associated_type_ref_impl(std::move(resolved), symbols, substitutions,
                                                    depth + 1);
        }
        return type;
    }
    const auto path = associated_type_path(type_ref_head_name(type));
    if (!path) {
        return type;
    }
    TypeRef owner = named_type_ref(path->first, type.location);
    if (const auto found = substitutions.find(path->first); found != substitutions.end()) {
        owner = found->second;
    }
    owner = receiver_template_type_ref(symbols, std::move(owner));
    const ClassDecl* owner_class = class_for_receiver_type(symbols, owner);
    if (owner_class == nullptr) {
        return type;
    }
    for (const TypeAliasDecl& alias : owner_class->type_aliases) {
        if (alias.name != path->second) {
            continue;
        }
        const std::vector<TypeRef> owner_args = template_arg_refs_from_type(owner);
        TypeRef resolved =
            substitute_receiver_template_type(alias.type_ref, *owner_class, owner_args);
        return resolve_associated_type_ref_impl(std::move(resolved), symbols, substitutions,
                                                depth + 1);
    }
    return type;
}

} // namespace

std::vector<TypeRef> template_arg_refs_from_type(const TypeRef& type) {
    if (type.kind == TypeKind::Shaped && !type.children.empty()) {
        return template_arg_refs_from_type(type.children.front());
    }
    return type.kind == TypeKind::Template ? type.children : std::vector<TypeRef>{};
}

TypeRef resolve_associated_type_ref(const Symbols& symbols, TypeRef type) {
    return resolve_associated_type_ref_impl(std::move(type), symbols, {}, 0);
}

TypeRef substitute_receiver_template_type(const TypeRef& type, const ClassDecl& klass,
                                          const std::vector<TypeRef>& receiver_args) {
    const std::map<std::string, TypeRef> substitutions =
        receiver_template_ref_substitutions(klass, receiver_args);
    if (substitutions.empty()) {
        return type;
    }
    return substitute_type_ref(type, substitutions);
}

TypeRef substitute_receiver_template_type(const TypeRef& type, const Symbols& symbols,
                                          const ClassDecl& klass,
                                          const std::vector<TypeRef>& receiver_args) {
    const std::map<std::string, TypeRef> substitutions =
        receiver_template_ref_substitutions(klass, receiver_args);
    if (substitutions.empty()) {
        return type;
    }
    return resolve_associated_type_ref_impl(type, symbols, substitutions, 0);
}

} // namespace dudu
