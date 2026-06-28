#include "dudu/sema_assignment.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_expr_internal.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/sema_scope.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

bool is_swizzle_name(std::string_view name) {
    if (name.size() < 2 || name.size() > 4) {
        return false;
    }
    for (const std::string_view set :
         {std::string_view("xyzw"), std::string_view("rgba"), std::string_view("stpq")}) {
        bool matches = true;
        for (const char ch : name) {
            if (set.find(ch) == std::string_view::npos) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

std::string indexed_assignment_label(const Expr& receiver) {
    const std::string label = expr_label(receiver);
    return label.empty() ? "indexed assignment" : label;
}

} // namespace

TypeRef assignment_target_type_ref(FunctionScope& scope, const Stmt& stmt) {
    const SourceLocation& target_location =
        diagnostic_location(stmt.location, stmt_target_expr(stmt));
    if (stmt_target_expr(stmt).kind == ExprKind::Unary && stmt_target_expr(stmt).op == "*" &&
        stmt_target_expr(stmt).children.size() == 1) {
        const Expr& pointee = stmt_target_expr(stmt).children.front();
        const TypeRef type = infer_expr_type_ast(scope, pointee, &target_location);
        if (!has_type_ref(type) || type_ref_is_auto(type)) {
            return {};
        }
        const auto pointee_type = unary_type_child_ref(type, TypeKind::Pointer);
        if (!pointee_type) {
            const std::string type_display = trim(substitute_type_ref_text(type, {}));
            sema_fail(target_location, "cannot dereference non-pointer: " + type_display);
        }
        return *pointee_type;
    }
    if (stmt_target_expr(stmt).kind == ExprKind::Index &&
        stmt_target_expr(stmt).children.size() == 2 &&
        stmt_target_expr(stmt).children[0].kind == ExprKind::Name) {
        const std::string& name = stmt_target_expr(stmt).children[0].name;
        if (scope.local_type_refs.contains(name)) {
            const TypeRef receiver_type = local_type_ref(scope, name, target_location);
            if (const auto signature =
                    dudu_operator_signature(scope.symbols, "[]=", receiver_type)) {
                std::vector<Expr> args = index_arg_exprs(stmt_target_expr(stmt).children[1]);
                args.push_back(stmt.value_expr);
                check_call_args_ast(scope, name + "[]=", *signature, args, &target_location);
                return {};
            }
        }
        return indexed_value_type_ref(scope.symbols, scope.local_type_refs, target_location, name,
                                      stmt_target_expr(stmt).children[1],
                                      "indexed assignment to unknown local: ");
    }
    if (stmt_target_expr(stmt).kind == ExprKind::Index &&
        stmt_target_expr(stmt).children.size() == 2) {
        const Expr& receiver = stmt_target_expr(stmt).children[0];
        const TypeRef receiver_type =
            member_expr_type_ref(scope.symbols, scope.local_type_refs, &target_location, receiver,
                                 {}, scope.current_class);
        if (has_type_ref(receiver_type)) {
            if (const auto signature =
                    dudu_operator_signature(scope.symbols, "[]=", receiver_type)) {
                std::vector<Expr> args = index_arg_exprs(stmt_target_expr(stmt).children[1]);
                args.push_back(stmt.value_expr);
                check_call_args_ast(scope, indexed_assignment_label(receiver) + "[]=", *signature,
                                    args, &target_location);
                return {};
            }
            return indexed_type_ref_from_type(scope.symbols, target_location, receiver_type,
                                              stmt_target_expr(stmt).children[1],
                                              indexed_assignment_label(receiver));
        }
    }
    if (stmt_target_expr(stmt).kind == ExprKind::Name) {
        const std::string& name = stmt_target_expr(stmt).name;
        if (scope.constants.contains(name)) {
            sema_fail(target_location, "cannot assign to constant: " + name);
        }
        if (!scope.local_type_refs.contains(name)) {
            sema_fail(target_location, "assignment to unknown local: " + name);
        }
        TypeRef type_ref = local_type_ref(scope, name, target_location);
        type_ref.location = target_location;
        return type_ref;
    }
    if (stmt_target_expr(stmt).kind == ExprKind::Member) {
        if (stmt_target_expr(stmt).children.size() == 1 &&
            is_swizzle_name(stmt_target_expr(stmt).name)) {
            const Expr& receiver = stmt_target_expr(stmt).children.front();
            const TypeRef receiver_type_ref =
                infer_expr_type_ast(scope, receiver, &target_location);
            if (const auto swizzle = swizzle_assignment_type_ref_for_type(
                    scope.symbols, target_location, receiver_type_ref,
                    stmt_target_expr(stmt).name)) {
                return *swizzle;
            }
        }
        if (const TypeRef type =
                member_expr_type_ref(scope.symbols, scope.local_type_refs, &target_location,
                                     stmt_target_expr(stmt), {}, scope.current_class);
            has_type_ref(type)) {
            return type;
        }
        sema_fail(target_location,
                  "unsupported assignment target: " + expr_label(stmt_target_expr(stmt)));
    }
    if (stmt_target_expr(stmt).kind == ExprKind::Call ||
        stmt_target_expr(stmt).kind == ExprKind::TemplateCall) {
        (void)infer_expr_type_ast(scope, stmt_target_expr(stmt), &target_location);
        return {};
    }
    if (has_stmt_target_expr(stmt)) {
        sema_fail(target_location,
                  "unsupported assignment target: " + expr_label(stmt_target_expr(stmt)));
    }
    sema_fail(target_location,
              "unsupported assignment target: " + expr_label(stmt_target_expr(stmt)));
}

} // namespace dudu
