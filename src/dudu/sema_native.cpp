#include "dudu/sema_native.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_signature_match.hpp"

#include <optional>

namespace dudu {

namespace {

std::optional<std::string> native_path_from_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        return expr.name;
    }
    if (expr.kind != ExprKind::Member || expr.children.size() != 1) {
        return std::nullopt;
    }
    const std::optional<std::string> receiver = native_path_from_expr(expr.children.front());
    if (!receiver) {
        return std::nullopt;
    }
    return *receiver + "." + expr.name;
}

} // namespace

bool foreign_cpp_type_name(const Symbols& symbols, const TypeRef& type) {
    TypeRef resolved_type = resolve_alias_ref(symbols, type);
    while (const auto inner = unary_type_child_ref(
               resolved_type,
               {TypeKind::Pointer, TypeKind::Reference, TypeKind::Const, TypeKind::Volatile,
                TypeKind::Atomic, TypeKind::Storage, TypeKind::Shared, TypeKind::Device})) {
        resolved_type = resolve_alias_ref(symbols, *inner);
    }
    const std::string head = type_ref_head_name(resolved_type);
    return head.find('.') != std::string::npos || head.find("::") != std::string::npos ||
           symbols.native_types.contains(base_type(resolved_type));
}

bool native_import_path_prefix(const Symbols& symbols, const std::string& path) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string prefix = path.substr(0, dot);
    return symbols.native_import_prefixes.contains(prefix) &&
           !symbols.module_import_prefixes.contains(prefix);
}

std::optional<TypeRef> native_member_path_type_ref(const Symbols& symbols, const std::string& path,
                                                   SourceLocation location) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        return std::nullopt;
    }
    if (const auto value = symbols.native_value_type_refs.find(path);
        value != symbols.native_value_type_refs.end()) {
        return value->second;
    }
    const std::string prefix = path.substr(0, dot);
    if (symbols.module_import_prefixes.contains(prefix)) {
        return std::nullopt;
    }
    if (prefix == "build" || prefix == "shader" || native_import_path_prefix(symbols, path)) {
        return named_type_ref("auto", location);
    }
    return std::nullopt;
}

std::optional<TypeRef> native_member_expr_type_ref(const Symbols& symbols, const Expr& expr,
                                                   SourceLocation location) {
    const std::optional<std::string> path = native_path_from_expr(expr);
    if (!path) {
        return std::nullopt;
    }
    return native_member_path_type_ref(symbols, *path, location);
}

std::optional<FunctionSignature>
native_signature_for_call(const FunctionScope& scope, const std::string& callee,
                          const std::vector<TypeRef>& explicit_template_args,
                          const std::vector<Expr>& args, const SourceLocation* location,
                          const NativeInferExprTypeAstFn& infer_expr_type,
                          const NativeCanAssignAstFn& can_assign) {
    return match_native_signature(scope, callee, explicit_template_args, args, location,
                                  infer_expr_type, can_assign);
}

} // namespace dudu
