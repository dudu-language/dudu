#include "dudu/sema/sema_body_substitution.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"

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
    for (Expr& callee : mutable_expr_callee(expr)) {
        substitute_expr_types(callee, substitutions);
    }
    for (Expr& arg : mutable_expr_template_args(expr)) {
        substitute_expr_types(arg, substitutions);
    }
}

void substitute_stmt_types(Stmt& stmt, const std::map<std::string, TypeRef>& substitutions) {
    if (has_stmt_type_ref(stmt)) {
        set_stmt_type_ref(stmt, substitute_type_ref(stmt_type_ref(stmt), substitutions));
    }
    substitute_expr_types(stmt.expr, substitutions);
    substitute_expr_types(stmt.value_expr, substitutions);
    if (has_stmt_target_expr(stmt)) {
        Expr target = stmt_target_expr(stmt);
        substitute_expr_types(target, substitutions);
        set_stmt_target_expr(stmt, std::move(target));
    }
    if (has_stmt_condition_expr(stmt)) {
        Expr condition = stmt_condition_expr(stmt);
        substitute_expr_types(condition, substitutions);
        set_stmt_condition_expr(stmt, std::move(condition));
    }
    if (has_stmt_message_expr(stmt)) {
        Expr message = stmt_message_expr(stmt);
        substitute_expr_types(message, substitutions);
        set_stmt_message_expr(stmt, std::move(message));
    }
    if (has_stmt_iterable_expr(stmt)) {
        Expr iterable = stmt_iterable_expr(stmt);
        substitute_expr_types(iterable, substitutions);
        set_stmt_iterable_expr(stmt, std::move(iterable));
    }
    if (has_stmt_pattern_expr(stmt)) {
        Expr pattern = stmt_pattern_expr(stmt);
        substitute_expr_types(pattern, substitutions);
        set_stmt_pattern_expr(stmt, std::move(pattern));
    }
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
