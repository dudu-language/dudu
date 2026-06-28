#include "dudu/sema_body_substitution.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"

#include <sstream>

namespace dudu {
namespace {

void substitute_expr_types(Expr& expr, const std::map<std::string, TypeRef>& substitutions) {
    if (const auto replacement = substitutions.find(expr.name);
        expr.kind == ExprKind::Name && replacement != substitutions.end()) {
        expr.name = substitute_type_ref_text(replacement->second, {});
    }
    if (has_expr_type_ref(expr)) {
        set_expr_type_ref(expr, substitute_type_ref(expr_type_ref(expr), substitutions));
    }
    if (has_expr_template_type_args(expr)) {
        std::vector<TypeRef> type_args = expr_template_type_args(expr);
        for (TypeRef& type_arg : type_args) {
            type_arg = substitute_type_ref(type_arg, substitutions);
        }
        set_expr_template_type_args(expr, std::move(type_args));
    }
    for (Expr& child : expr.children) {
        substitute_expr_types(child, substitutions);
    }
    for (Expr& callee : expr.callee) {
        substitute_expr_types(callee, substitutions);
    }
    for (Expr& arg : expr.template_args) {
        substitute_expr_types(arg, substitutions);
    }
}

void substitute_stmt_types(Stmt& stmt, const std::map<std::string, TypeRef>& substitutions) {
    if (has_stmt_type_ref(stmt)) {
        set_stmt_type_ref(stmt, substitute_type_ref(stmt_type_ref(stmt), substitutions));
    }
    substitute_expr_types(stmt.expr, substitutions);
    substitute_expr_types(stmt.value_expr, substitutions);
    substitute_expr_types(stmt.target_expr, substitutions);
    substitute_expr_types(stmt.condition_expr, substitutions);
    if (has_stmt_message_expr(stmt)) {
        Expr message = stmt_message_expr(stmt);
        substitute_expr_types(message, substitutions);
        set_stmt_message_expr(stmt, std::move(message));
    }
    substitute_expr_types(stmt.iterable_expr, substitutions);
    substitute_expr_types(stmt.pattern_expr, substitutions);
    if (has_stmt_guard_expr(stmt)) {
        Expr guard = stmt_guard_expr(stmt);
        substitute_expr_types(guard, substitutions);
        set_stmt_guard_expr(stmt, std::move(guard));
    }
    for (Stmt& child : stmt.children) {
        substitute_stmt_types(child, substitutions);
    }
}

} // namespace

std::map<std::string, TypeRef> body_type_substitutions(const std::vector<std::string>& params,
                                                       const std::vector<TypeRef>& args) {
    std::map<std::string, TypeRef> out;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        out.emplace(params[i], args[i]);
    }
    return out;
}

std::string body_instantiated_label(const std::string& name, const std::vector<TypeRef>& args) {
    std::ostringstream out;
    out << name << "[";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(args[i], {});
    }
    out << "]";
    return out.str();
}

std::vector<Stmt> substitute_body_types(std::vector<Stmt> body,
                                        const std::map<std::string, TypeRef>& substitutions) {
    for (Stmt& stmt : body) {
        substitute_stmt_types(stmt, substitutions);
    }
    return body;
}

} // namespace dudu
