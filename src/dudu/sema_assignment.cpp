#include "dudu/sema_assignment.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
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

} // namespace

TypeRef assignment_target_type_ref(FunctionScope& scope, const Stmt& stmt,
                                   const BodyCheckCallbacks& callbacks) {
    const SourceLocation& target_location = node_location(stmt.location, stmt.target_expr);
    if (stmt.target_expr.kind == ExprKind::Unary && stmt.target_expr.op == "*" &&
        stmt.target_expr.children.size() == 1) {
        const Expr& pointee = stmt.target_expr.children.front();
        const TypeRef type = callbacks.infer_expr_type(scope, pointee, &target_location);
        const std::string type_text = trim(substitute_type_ref_text(type, {}));
        if (type_text.empty() || type_text == "auto") {
            return {};
        }
        const auto pointee_type = unary_type_child_ref(type, TypeKind::Pointer);
        if (!pointee_type) {
            sema_fail(target_location, "cannot dereference non-pointer: " + type_text);
        }
        return *pointee_type;
    }
    if (stmt.target_expr.kind == ExprKind::Index && stmt.target_expr.children.size() == 2 &&
        stmt.target_expr.children[0].kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.children[0].name;
        if (scope.local_type_refs.contains(name)) {
            const TypeRef receiver_type = local_type_ref(scope, name, target_location);
            const std::string receiver_type_text = substitute_type_ref_text(receiver_type, {});
            if (const auto signature =
                    dudu_operator_signature(scope.symbols, "[]=", receiver_type_text)) {
                std::vector<Expr> args = index_arg_exprs(stmt.target_expr.children[1]);
                args.push_back(stmt.value_expr);
                callbacks.check_call_args(scope, name + "[]=", *signature, args, &target_location);
                return {};
            }
        }
        return indexed_value_type_ref(scope.symbols, scope.local_type_refs, target_location, name,
                                      stmt.target_expr.children[1],
                                      "indexed assignment to unknown local: ");
    }
    if (stmt.target_expr.kind == ExprKind::Index && stmt.target_expr.children.size() == 2) {
        const Expr& receiver = stmt.target_expr.children[0];
        const TypeRef receiver_type =
            member_expr_type_ref(scope.symbols, scope.local_type_refs, &target_location, receiver,
                                 {}, scope.current_class);
        if (has_type_ref(receiver_type)) {
            return indexed_type_ref_from_type(
                scope.symbols, target_location, receiver_type, stmt.target_expr.children[1],
                display_expr(receiver).empty() ? "indexed assignment" : display_expr(receiver));
        }
    }
    if (stmt.target_expr.kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.name;
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
    if (stmt.target_expr.kind == ExprKind::Member) {
        if (stmt.target_expr.children.size() == 1 && is_swizzle_name(stmt.target_expr.name)) {
            const Expr& receiver = stmt.target_expr.children.front();
            const TypeRef receiver_type_ref =
                callbacks.infer_expr_type(scope, receiver, &target_location);
            if (const auto swizzle = swizzle_assignment_type_ref_for_type(
                    scope.symbols, target_location, receiver_type_ref, stmt.target_expr.name)) {
                return *swizzle;
            }
        }
        if (const TypeRef type =
                member_expr_type_ref(scope.symbols, scope.local_type_refs, &target_location,
                                     stmt.target_expr, {}, scope.current_class);
            has_type_ref(type)) {
            return type;
        }
        sema_fail(target_location,
                  "unsupported assignment target: " + display_expr(stmt.target_expr));
    }
    if (stmt.target_expr.kind == ExprKind::Call ||
        stmt.target_expr.kind == ExprKind::TemplateCall) {
        (void)callbacks.infer_expr_type(scope, stmt.target_expr, &target_location);
        return {};
    }
    if (expr_present(stmt.target_expr)) {
        sema_fail(target_location,
                  "unsupported assignment target: " + display_expr(stmt.target_expr));
    }
    sema_fail(target_location, "unsupported assignment target: " + display_expr(stmt.target_expr));
}

std::string assignment_target_type(FunctionScope& scope, const Stmt& stmt,
                                   const BodyCheckCallbacks& callbacks) {
    return substitute_type_ref_text(assignment_target_type_ref(scope, stmt, callbacks), {});
}

} // namespace dudu
