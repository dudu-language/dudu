#include "dudu/sema/sema_method_templates.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <map>
#include <optional>

namespace dudu {
namespace {

std::map<std::string, TypeRef>
receiver_template_ref_substitutions(const ClassDecl& klass,
                                    const std::vector<TypeRef>& receiver_args) {
    std::map<std::string, TypeRef> substitutions;
    const std::vector<TypeRef> concrete_args = generic_args_with_defaults(
        klass.generic_params, klass.generic_default_args, receiver_args);
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

TypeRef resolve_associated_type_ref(TypeRef type, const Symbols& symbols,
                                    const std::map<std::string, TypeRef>& substitutions,
                                    size_t depth) {
    type = substitute_type_ref(type, substitutions);
    if (depth > 16) {
        return type;
    }
    for (TypeRef& child : type.children) {
        child = resolve_associated_type_ref(std::move(child), symbols, substitutions, depth + 1);
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
        return resolve_associated_type_ref(std::move(resolved), symbols, substitutions, depth + 1);
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
    return resolve_associated_type_ref(type, symbols, substitutions, 0);
}

} // namespace dudu
