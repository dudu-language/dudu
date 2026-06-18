#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

TypeRef named_member_type_ref(std::string name, SourceLocation location) {
    TypeRef type;
    type.kind = TypeKind::Named;
    type.name = std::move(name);
    type.location = location;
    type.text = type.name;
    return type;
}

} // namespace

std::optional<TypeRef> member_expr_direct_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location) {
    if (expr.kind != ExprKind::Member) {
        return std::nullopt;
    }
    const SourceLocation type_location = location == nullptr ? expr.location : *location;
    if (const auto variant = enum_variant_from_expr(scope.symbols, expr)) {
        if (!variant->second->payload_fields.empty() && location != nullptr) {
            sema_expr_fail(*location, "payload enum variant requires construction: " +
                                          variant->first->name + "." + variant->second->name);
        }
        return named_member_type_ref(variant->first->name, expr.location);
    }
    if (const auto native = native_member_expr_type_ref(scope.symbols, expr, type_location)) {
        return *native;
    }
    if (const TypeRef found = member_expr_type_ref(scope.symbols, scope.local_type_refs, location,
                                                   expr, {}, scope.current_class);
        has_type_ref(found)) {
        return found;
    }
    if (expr.children.size() != 1) {
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported member expression: " + display_expr(expr));
        }
        return std::nullopt;
    }
    const Expr& receiver = expr.children.front();
    const TypeRef receiver_ref = infer_expr_type_ast(scope, receiver, location);
    const std::string receiver_type = substitute_type_ref_text(receiver_ref, {});
    if (receiver_type.empty()) {
        return std::nullopt;
    }
    if (const auto field = field_type_ref_for_type(scope.symbols, receiver_ref, expr.name)) {
        return *field;
    }
    if (const auto swizzle = swizzle_type_ref_for_type(scope.symbols, receiver_ref, expr.name)) {
        return *swizzle;
    }
    if (foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type))) {
        return named_member_type_ref("auto", expr.location);
    }
    if (location != nullptr) {
        sema_expr_fail(*location, "unknown field: " + receiver_type + "." + expr.name);
    }
    return std::nullopt;
}

} // namespace dudu
