#include "dudu/sema/sema_enum.hpp"

#include "dudu/core/ast_type.hpp"

namespace dudu {

const EnumDecl* enum_decl_for_type(const Symbols& symbols, const TypeRef& type) {
    if (const auto exact = symbols.enums.find(type_ref_head_name(type));
        exact != symbols.enums.end()) {
        return exact->second;
    }
    TypeRef resolved = resolve_alias_ref(symbols, type);
    while ((resolved.kind == TypeKind::Reference || resolved.kind == TypeKind::Pointer ||
            resolved.kind == TypeKind::Const) &&
           resolved.children.size() == 1) {
        resolved = resolve_alias_ref(symbols, resolved.children.front());
    }
    const auto found = symbols.enums.find(type_ref_head_name(resolved));
    return found == symbols.enums.end() ? nullptr : found->second;
}

const EnumValueDecl* enum_variant_decl(const EnumDecl& en, const std::string& variant) {
    for (const EnumValueDecl& value : en.values) {
        if (value.name == variant) {
            return &value;
        }
    }
    return nullptr;
}

bool enum_has_payloads(const EnumDecl& en) {
    for (const EnumValueDecl& value : en.values) {
        if (!value.payload_fields.empty()) {
            return true;
        }
    }
    return false;
}

std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_path(const Symbols& symbols, const std::string& path) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos || path.find('.', dot + 1) != std::string::npos) {
        return std::nullopt;
    }
    const std::string enum_name = path.substr(0, dot);
    const auto en = symbols.enums.find(enum_name);
    if (en == symbols.enums.end()) {
        return std::nullopt;
    }
    const EnumValueDecl* value = enum_variant_decl(*en->second, path.substr(dot + 1));
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(en->second, value);
}

std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_expr(const Symbols& symbols, const Expr& expr) {
    if (expr.kind != ExprKind::Member || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Name) {
        return std::nullopt;
    }
    const auto en = symbols.enums.find(expr.children.front().name);
    if (en == symbols.enums.end()) {
        return std::nullopt;
    }
    const EnumValueDecl* value = enum_variant_decl(*en->second, expr.name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(en->second, value);
}

} // namespace dudu
