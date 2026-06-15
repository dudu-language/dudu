#include "dudu/sema_assignment.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_ops.hpp"

#include <optional>
#include <vector>

namespace dudu {

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
        if (type.front() != '*') {
            sema_fail(target_location, "cannot dereference non-pointer: " + type);
        }
        return trim(type.substr(1));
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
        if (const std::optional<std::string> receiver_path = member_path_from_expr(receiver)) {
            const std::string normalized_receiver =
                normalize_current_class_path(scope, *receiver_path, &target_location);
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, &target_location, normalized_receiver,
                                 "assignment through unknown local: ");
            if (!receiver_type.empty()) {
                return indexed_type_from_type(scope.symbols, target_location, receiver_type,
                                              stmt.target_expr.children[1], normalized_receiver);
            }
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
        if (const std::optional<std::string> path = member_path_from_expr(stmt.target_expr)) {
            return member_path_type(scope.symbols, scope.locals, &target_location,
                                    normalize_current_class_path(scope, *path, &target_location),
                                    "assignment through unknown local: ");
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
