#include "dudu/sema/sema_method_templates.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_generics_detail.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/sema_native_specializations.hpp"
#include "dudu/sema/sema_native_type_transforms.hpp"

#include <map>
#include <optional>

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

GenericTypeBindings owner_member_bindings(const ClassDecl& owner_class, const TypeRef& owner,
                                          const GenericTypeBindings& specialization_bindings) {
    GenericTypeBindings bindings = specialization_bindings;
    const std::vector<TypeRef> owner_args = template_arg_refs_from_type(owner);
    const auto owner_substitutions = receiver_template_ref_substitutions(owner_class, owner_args);
    bindings.scalar.insert(owner_substitutions.begin(), owner_substitutions.end());
    return bindings;
}

TypeRef instantiate_member_alias(const TypeAliasDecl& alias,
                                 const std::vector<TypeRef>& member_args,
                                 GenericTypeBindings bindings) {
    const std::vector<TypeRef> concrete_args =
        generic_args_with_defaults(alias.generic_params, alias.generic_default_args, member_args);
    for (size_t i = 0; i < alias.generic_params.size() && i < concrete_args.size(); ++i) {
        bindings.scalar.insert_or_assign(generic_param_base_name(alias.generic_params[i]),
                                         concrete_args[i]);
    }
    return substitute_native_specialization_type(alias.type_ref, bindings);
}

TypeRef canonical_native_associated_scalar(std::string_view alias_name, TypeRef resolved) {
    const std::string type_name = type_ref_head_name(resolved);
    if (alias_name == "size_type" &&
        (type_name == "u8" || type_name == "u16" || type_name == "u32" || type_name == "u64" ||
         type_name == "usize")) {
        return named_type_ref("usize", resolved.location);
    }
    if (alias_name == "difference_type" &&
        (type_name == "i8" || type_name == "i16" || type_name == "i32" || type_name == "i64" ||
         type_name == "isize")) {
        return named_type_ref("isize", resolved.location);
    }
    return resolved;
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
    if (type.kind == TypeKind::NativeTransform) {
        TypeRef resolved = resolve_native_type_transform(symbols, type);
        if (!type_ref_equivalent(resolved, type)) {
            return resolve_associated_type_ref_impl(std::move(resolved), symbols, substitutions,
                                                    depth + 1);
        }
        return type;
    }
    if (type.kind == TypeKind::AssociatedTemplate && type.children.size() > 1) {
        TypeRef owner = receiver_template_type_ref(symbols, type.children.front());
        GenericTypeBindings specialization_bindings;
        const ClassDecl* owner_class =
            native_specialized_class_for_owner(symbols, owner, specialization_bindings);
        if (owner_class == nullptr) {
            owner_class = class_for_receiver_type(symbols, owner);
        }
        if (owner_class != nullptr) {
            const std::vector<TypeRef> member_args(type.children.begin() + 1, type.children.end());
            for (const TypeAliasDecl& alias : owner_class->type_aliases) {
                if (alias.name != type.name) {
                    continue;
                }
                TypeRef resolved = instantiate_member_alias(
                    alias, member_args,
                    owner_member_bindings(*owner_class, owner, specialization_bindings));
                return canonical_native_associated_scalar(
                    alias.name, resolve_associated_type_ref_impl(std::move(resolved), symbols,
                                                                 substitutions, depth + 1));
            }
        } else {
            const std::string owner_name = receiver_class_name(symbols, owner);
            const std::string alias_name = owner_name.empty()
                                               ? std::string(type.name)
                                               : owner_name + "." + std::string(type.name);
            if (symbols.alias_type_refs.contains(alias_name)) {
                TypeRef alias_use;
                alias_use.kind = TypeKind::Template;
                alias_use.name = alias_name;
                alias_use.location = type.location;
                alias_use.children.assign(type.children.begin() + 1, type.children.end());
                TypeRef resolved = resolve_alias_ref(symbols, std::move(alias_use));
                return resolve_associated_type_ref_impl(std::move(resolved), symbols, substitutions,
                                                        depth + 1);
            }
        }
    }
    if (type.kind == TypeKind::Associated && type.children.size() == 1) {
        TypeRef owner = receiver_template_type_ref(symbols, type.children.front());
        GenericTypeBindings specialization_bindings;
        const ClassDecl* owner_class =
            native_specialized_class_for_owner(symbols, owner, specialization_bindings);
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
            if (!specialization_bindings.scalar.empty() || !specialization_bindings.packs.empty()) {
                resolved =
                    substitute_native_specialization_type(alias.type_ref, specialization_bindings);
                std::map<std::string, TypeRef> nested_substitutions = substitutions;
                nested_substitutions.insert(specialization_bindings.scalar.begin(),
                                            specialization_bindings.scalar.end());
                return canonical_native_associated_scalar(
                    alias.name, resolve_associated_type_ref_impl(std::move(resolved), symbols,
                                                                 nested_substitutions, depth + 1));
            } else if (owner.kind == TypeKind::AssociatedTemplate && !owner.children.empty()) {
                std::map<std::string, TypeRef> nested_substitutions = substitutions;
                const TypeRef& parent_owner = owner.children.front();
                if (const ClassDecl* parent_class =
                        class_for_receiver_type(symbols, parent_owner)) {
                    const std::vector<TypeRef> parent_args =
                        template_arg_refs_from_type(parent_owner);
                    const auto parent_substitutions =
                        receiver_template_ref_substitutions(*parent_class, parent_args);
                    nested_substitutions.insert(parent_substitutions.begin(),
                                                parent_substitutions.end());
                }
                const std::vector<TypeRef> owner_args = template_arg_refs_from_type(owner);
                const auto member_substitutions =
                    receiver_template_ref_substitutions(*owner_class, owner_args);
                nested_substitutions.insert(member_substitutions.begin(),
                                            member_substitutions.end());
                resolved = substitute_type_ref(alias.type_ref, nested_substitutions);
                return canonical_native_associated_scalar(
                    alias.name, resolve_associated_type_ref_impl(std::move(resolved), symbols,
                                                                 nested_substitutions, depth + 1));
            } else {
                const std::vector<TypeRef> owner_args = template_arg_refs_from_type(owner);
                resolved =
                    substitute_receiver_template_type(alias.type_ref, *owner_class, owner_args);
            }
            return canonical_native_associated_scalar(
                alias.name, resolve_associated_type_ref_impl(std::move(resolved), symbols,
                                                             substitutions, depth + 1));
        }
        const GenericTypeBindings member_bindings =
            owner_member_bindings(*owner_class, owner, specialization_bindings);
        for (const ConstDecl& field : owner_class->static_fields) {
            if (field.name != type.name) {
                continue;
            }
            if (const auto value = native_constexpr_value_ref(
                    field.value_expr, member_bindings.scalar, type.location)) {
                return *value;
            }
            return type;
        }
        const std::vector<TypeRef> owner_args = template_arg_refs_from_type(owner);
        std::map<std::string, TypeRef> owner_substitutions =
            receiver_template_ref_substitutions(*owner_class, owner_args);
        owner_substitutions.insert(specialization_bindings.scalar.begin(),
                                   specialization_bindings.scalar.end());
        GenericTypeBindings inherited_bindings;
        inherited_bindings.scalar = owner_substitutions;
        inherited_bindings.packs = specialization_bindings.packs;
        for (const BaseClassDecl& base : owner_class->base_class_refs) {
            TypeRef inherited;
            inherited.kind = TypeKind::Associated;
            inherited.name = type.name;
            inherited.children.push_back(
                substitute_native_specialization_type(base.type_ref, inherited_bindings));
            inherited.location = type.location;
            const std::string inherited_text = type_ref_text(inherited);
            TypeRef resolved = resolve_associated_type_ref_impl(std::move(inherited), symbols,
                                                                substitutions, depth + 1);
            if (type_ref_text(resolved) != inherited_text) {
                return resolved;
            }
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
        return canonical_native_associated_scalar(
            alias.name, resolve_associated_type_ref_impl(std::move(resolved), symbols,
                                                         substitutions, depth + 1));
    }
    return type;
}

} // namespace

std::vector<TypeRef> template_arg_refs_from_type(const TypeRef& type) {
    if (type.kind == TypeKind::Shaped && !type.children.empty()) {
        return template_arg_refs_from_type(type.children.front());
    }
    if (type.kind == TypeKind::Template) {
        return type.children;
    }
    if (type.kind == TypeKind::AssociatedTemplate && type.children.size() > 1) {
        return {type.children.begin() + 1, type.children.end()};
    }
    return {};
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
