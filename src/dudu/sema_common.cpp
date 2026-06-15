#include "dudu/sema_common.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

namespace dudu {

[[noreturn]] void sema_fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool sema_has_expr(const Expr& expr) {
    return !expr.text.empty();
}

bool has_type_ref(const TypeRef& type) {
    return type.kind != TypeKind::Unknown || !type.text.empty();
}

bool missing_expr(const Expr& expr) {
    return expr.text.empty() || (expr.kind == ExprKind::Unknown && trim(expr.text).empty());
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

} // namespace dudu
