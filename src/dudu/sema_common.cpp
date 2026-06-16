#include "dudu/sema_common.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

namespace dudu {

[[noreturn]] void sema_fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool has_type_ref(const TypeRef& type) {
    return type.kind != TypeKind::Unknown || !type.text.empty();
}

bool missing_expr(const Expr& expr) {
    return expr_missing(expr);
}

bool sema_has_expr(const Expr& expr) {
    return expr_present(expr);
}

const SourceLocation& node_location(const SourceLocation& fallback, const Expr& expr) {
    return expr.range.end.column > expr.range.start.column ? expr.location : fallback;
}

const SourceLocation& node_location(const SourceLocation& fallback, const TypeRef& type) {
    return type.range.end.column > type.range.start.column ? type.location : fallback;
}

void bind_local(FunctionScope& scope, const std::string& name, const std::string& type,
                const TypeRef& type_ref) {
    scope.locals[name] = type;
    if (type_ref.kind != TypeKind::Unknown || !type_ref.text.empty()) {
        scope.local_type_refs[name] = type_ref;
    } else {
        scope.local_type_refs.erase(name);
    }
}

std::vector<Expr> index_arg_exprs(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children;
    }
    return {index_expr};
}

std::optional<std::string> scoped_member_path_from_expr(const FunctionScope& scope,
                                                        const Expr& expr,
                                                        const SourceLocation* location) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        if (expr.name == "class") {
            if (!scope.current_class.empty()) {
                return scope.current_class;
            }
            if (location != nullptr) {
                sema_fail(*location, "class static access outside class");
            }
            return std::nullopt;
        }
        return expr.name;
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 && !expr.name.empty()) {
        const std::optional<std::string> receiver =
            scoped_member_path_from_expr(scope, expr.children.front(), location);
        if (receiver.has_value()) {
            return *receiver + "." + expr.name;
        }
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
        const std::optional<std::string> receiver =
            scoped_member_path_from_expr(scope, expr.children.front(), location);
        if (receiver.has_value() && !expr.children[1].text.empty()) {
            return *receiver + "[" + expr.children[1].text + "]";
        }
    }
    return std::nullopt;
}

std::string scoped_call_callee_text(const FunctionScope& scope, const Expr& expr,
                                    const SourceLocation* location) {
    if (!expr.callee.empty()) {
        if (const std::optional<std::string> path =
                scoped_member_path_from_expr(scope, expr.callee.front(), location)) {
            return *path;
        }
    }
    return trim_copy(expr.name);
}

} // namespace dudu
