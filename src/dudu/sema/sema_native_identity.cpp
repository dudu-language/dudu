#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_context.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

std::string native_identity_atom(std::string_view identity) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "__dudu_native_identity_";
    out.reserve(out.size() + identity.size() * 2);
    for (const unsigned char ch : identity) {
        out.push_back(hex[ch >> 4]);
        out.push_back(hex[ch & 0x0f]);
    }
    return out;
}

} // namespace

TypeRef canonical_native_type_ref(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    for (TypeRef& child : type.children) {
        child = canonical_native_type_ref(symbols, std::move(child));
    }
    if (type.kind == TypeKind::Named || type.kind == TypeKind::Qualified ||
        type.kind == TypeKind::Template) {
        const std::string binding = type_ref_head_name(type);
        const auto identity = symbols.native_type_identity_by_binding.find(binding);
        if (identity != symbols.native_type_identity_by_binding.end()) {
            type.name = native_identity_atom(identity->second);
            if (type.kind == TypeKind::Qualified) {
                type.kind = TypeKind::Named;
            }
        } else if (const auto klass = symbols.classes.find(binding);
                   klass != symbols.classes.end() && !klass->second->origin_module.empty()) {
            type.name = native_identity_atom("path:" + klass->second->origin_module + "." +
                                             klass->second->name);
            if (type.kind == TypeKind::Qualified) {
                type.kind = TypeKind::Named;
            }
        } else if (const auto en = symbols.enums.find(binding);
                   en != symbols.enums.end() && !en->second->origin_module.empty()) {
            type.name =
                native_identity_atom("path:" + en->second->origin_module + "." + en->second->name);
            if (type.kind == TypeKind::Qualified) {
                type.kind = TypeKind::Named;
            }
        }
    }
    return type;
}

bool is_canonical_native_type_ref(const TypeRef& type) {
    return (type.kind == TypeKind::Named || type.kind == TypeKind::Qualified ||
            type.kind == TypeKind::Template) &&
           type_ref_head_name(type).starts_with("__dudu_native_identity_");
}

const NativeTypeDecl* native_type_decl_for_binding(const Symbols& symbols,
                                                   std::string_view binding) {
    const auto identity = symbols.native_type_identity_by_binding.find(std::string(binding));
    if (identity == symbols.native_type_identity_by_binding.end()) {
        return nullptr;
    }
    const auto declarations = symbols.native_type_decls_by_identity.find(identity->second);
    if (declarations == symbols.native_type_decls_by_identity.end()) {
        return nullptr;
    }
    const auto declaration = declarations->second.find(std::string(binding));
    return declaration == declarations->second.end() ? nullptr : declaration->second;
}

const ClassDecl* native_class_decl_for_binding(const Symbols& symbols, std::string_view binding) {
    const auto identity = symbols.native_type_identity_by_binding.find(std::string(binding));
    if (identity == symbols.native_type_identity_by_binding.end()) {
        return nullptr;
    }
    const auto declarations = symbols.native_class_decls_by_identity.find(identity->second);
    if (declarations == symbols.native_class_decls_by_identity.end()) {
        return nullptr;
    }
    const auto declaration = declarations->second.find(std::string(binding));
    return declaration == declarations->second.end() ? nullptr : declaration->second;
}

bool is_native_class_binding(const Symbols& symbols, std::string_view binding) {
    return native_class_decl_for_binding(symbols, binding) != nullptr;
}

const NativeFunctionDecl* native_function_decl_for_overload(const Symbols& symbols,
                                                            std::string_view binding,
                                                            size_t overload_index) {
    const auto identities =
        symbols.native_function_identities_by_binding.find(std::string(binding));
    if (identities == symbols.native_function_identities_by_binding.end() ||
        overload_index >= identities->second.size()) {
        return nullptr;
    }
    const auto declaration =
        symbols.native_function_decls_by_identity.find(identities->second[overload_index]);
    if (declaration == symbols.native_function_decls_by_identity.end()) {
        return nullptr;
    }
    const auto binding_decl = declaration->second.find(std::string(binding));
    return binding_decl == declaration->second.end() ? nullptr : binding_decl->second;
}

std::vector<const NativeFunctionDecl*> native_function_decls_for_binding(const Symbols& symbols,
                                                                         std::string_view binding) {
    std::vector<const NativeFunctionDecl*> out;
    const auto identities =
        symbols.native_function_identities_by_binding.find(std::string(binding));
    if (identities == symbols.native_function_identities_by_binding.end()) {
        return out;
    }
    out.reserve(identities->second.size());
    for (const std::string& identity : identities->second) {
        if (const auto declaration = symbols.native_function_decls_by_identity.find(identity);
            declaration != symbols.native_function_decls_by_identity.end()) {
            if (const auto binding_decl = declaration->second.find(std::string(binding));
                binding_decl != declaration->second.end()) {
                out.push_back(binding_decl->second);
            }
        }
    }
    return out;
}

} // namespace dudu
