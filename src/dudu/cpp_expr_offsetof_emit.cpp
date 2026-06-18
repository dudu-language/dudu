#include "dudu/ast_expr.hpp"
#include "dudu/cpp_expr_call_emit.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"

#include <optional>

namespace dudu {

namespace {

std::string unquoted_string_literal(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

} // namespace

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const CppLocalContext& locals,
                                 const Symbols* symbols, const CppEmitOptions& options) {
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
        return unquoted_string_literal(expr.text);
    }
    if (expr.kind == ExprKind::Member) {
        if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
            return render_expr_path(*path);
        }
    }
    return lower_expr(expr, aliases, locals, local_type_refs, symbols, options);
}

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const CppLocalContext& locals,
                                 const Symbols* symbols) {
    return lower_offsetof_field(expr, aliases, locals, symbols, {});
}

} // namespace dudu
