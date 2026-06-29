#include "dudu/core/ast_expr.hpp"
#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"

#include <optional>

namespace dudu {

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const CppLocalContext& locals, const Symbols* symbols,
                                 const CppEmitOptions& options) {
    return lower_offsetof_field(expr, aliases, locals, {}, symbols, options);
}

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const CppLocalContext& locals,
                                 const std::map<std::string, TypeRef>& local_type_refs,
                                 const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        return expr.name;
    }
    if (expr.kind == ExprKind::StringLiteral) {
        if (expr.value.empty()) {
            throw CompileError(expr.location,
                               "malformed string literal node: missing parsed value");
        }
        return expr.value;
    }
    if (expr.kind == ExprKind::Member) {
        if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
            return render_expr_path(*path);
        }
    }
    return lower_expr(expr, aliases, locals, local_type_refs, symbols, options);
}

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const CppLocalContext& locals, const Symbols* symbols) {
    return lower_offsetof_field(expr, aliases, locals, symbols, {});
}

} // namespace dudu
