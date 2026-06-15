#include "dudu/sema_assignment.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_ops.hpp"

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

std::string assignment_target_type(FunctionScope& scope, const Stmt& stmt,
                                   const BodyCheckCallbacks& callbacks) {
    const SourceLocation& target_location = node_location(stmt.location, stmt.target_expr);
    if (stmt.target_expr.kind == ExprKind::Unary && stmt.target_expr.op == "*" &&
        stmt.target_expr.children.size() == 1) {
        const Expr& pointee = stmt.target_expr.children.front();
        std::string type = trim(callbacks.infer_expr(scope, pointee, &target_location));
        if (type.empty() || type == "auto") {
            return {};
        }
        const auto pointee_type = unary_type_child_text(type, TypeKind::Pointer);
        if (!pointee_type) {
            sema_fail(target_location, "cannot dereference non-pointer: " + type);
        }
        return *pointee_type;
    }
    if (stmt.target_expr.kind == ExprKind::Index && stmt.target_expr.children.size() == 2 &&
        stmt.target_expr.children[0].kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.children[0].name;
        if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
            if (const auto signature =
                    dudu_operator_signature(scope.symbols, "[]=", local->second)) {
                std::vector<Expr> args = index_arg_exprs(stmt.target_expr.children[1]);
                args.push_back(stmt.value_expr);
                callbacks.check_call_args(scope, name + "[]=", *signature, args, &target_location);
                return {};
            }
        }
        return indexed_value_type(scope.symbols, scope.locals, target_location, name,
                                  stmt.target_expr.children[1],
                                  "indexed assignment to unknown local: ");
    }
    if (stmt.target_expr.kind == ExprKind::Index && stmt.target_expr.children.size() == 2) {
        const Expr& receiver = stmt.target_expr.children[0];
        const std::string receiver_type = member_expr_type(
            scope.symbols, scope.locals, &target_location, receiver, {}, scope.current_class);
        if (!receiver_type.empty()) {
            return indexed_type_from_type(
                scope.symbols, target_location, receiver_type, stmt.target_expr.children[1],
                receiver.text.empty() ? "indexed assignment" : receiver.text);
        }
    }
    if (stmt.target_expr.kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.name;
        if (scope.constants.contains(name)) {
            sema_fail(target_location, "cannot assign to constant: " + name);
        }
        const auto local = scope.locals.find(name);
        if (local == scope.locals.end()) {
            sema_fail(target_location, "assignment to unknown local: " + name);
        }
        return local->second;
    }
    if (stmt.target_expr.kind == ExprKind::Member) {
        if (stmt.target_expr.children.size() == 1 && is_swizzle_name(stmt.target_expr.name)) {
            const Expr& receiver = stmt.target_expr.children.front();
            const std::string receiver_type =
                callbacks.infer_expr(scope, receiver, &target_location);
            if (const auto swizzle = swizzle_assignment_type_for_type(
                    scope.symbols, target_location, receiver_type, stmt.target_expr.name)) {
                return *swizzle;
            }
        }
        if (const std::string type = member_expr_type(scope.symbols, scope.locals, &target_location,
                                                      stmt.target_expr, {}, scope.current_class);
            !type.empty()) {
            return type;
        }
        sema_fail(target_location, "unsupported assignment target: " + stmt.target_expr.text);
    }
    if (stmt.target_expr.kind == ExprKind::Call ||
        stmt.target_expr.kind == ExprKind::TemplateCall) {
        (void)callbacks.infer_expr(scope, stmt.target_expr, &target_location);
        return {};
    }
    if (stmt.target_expr.kind != ExprKind::Unknown) {
        sema_fail(target_location, "unsupported assignment target: " + stmt.target_expr.text);
    }
    sema_fail(target_location, "unsupported assignment target: " + trim(stmt.target_expr.text));
}

} // namespace dudu
