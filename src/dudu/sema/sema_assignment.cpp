#include "dudu/sema/sema_assignment.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_bindings.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/sema_scope.hpp"

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

std::vector<TypeRef> infer_assignment_arg_type_refs(const FunctionScope& scope,
                                                    const std::vector<Expr>& args,
                                                    const SourceLocation* location) {
    std::vector<TypeRef> out;
    out.reserve(args.size());
    for (const Expr& arg : args) {
        out.push_back(infer_expr_type_ast(scope, arg, location));
    }
    return out;
}

TypeRef resolved_assignment_type(const Symbols& symbols, TypeRef type) {
    while (true) {
        const TypeRef resolved = resolve_alias_ref(symbols, type);
        if (!type_ref_same_shape(resolved, type)) {
            type = resolved;
            continue;
        }
        return type;
    }
}

bool member_assignment_receiver_is_const(const Symbols& symbols, TypeRef type) {
    type = resolved_assignment_type(symbols, std::move(type));
    if (type.kind == TypeKind::Const) {
        return true;
    }
    if ((type.kind == TypeKind::Reference || type.kind == TypeKind::Pointer) &&
        type.children.size() == 1) {
        TypeRef target = resolved_assignment_type(symbols, type.children.front());
        return target.kind == TypeKind::Const;
    }
    return false;
}

void reject_const_member_assignment(const FunctionScope& scope, const Expr& target,
                                    const SourceLocation& location) {
    if (target.kind != ExprKind::Member || target.children.size() != 1) {
        return;
    }
    const TypeRef receiver_type = infer_expr_type_ast(scope, target.children.front(), &location);
    if (has_type_ref(receiver_type) &&
        member_assignment_receiver_is_const(scope.symbols, receiver_type)) {
        const std::string label = expr_label(target);
        sema_fail(location, "cannot assign to member through const receiver" +
                                (label.empty() ? std::string{} : ": " + label));
    }
}

void check_declared_index_assignment_operator_if_any(const FunctionScope& scope,
                                                     const TypeRef& receiver_type,
                                                     const std::string& label,
                                                     const std::vector<Expr>& args,
                                                     const SourceLocation& location) {
    if (const auto signature = dudu_operator_signature(scope.symbols, "[]=", receiver_type)) {
        check_call_args_ast(scope, label + "[]=", *signature, args, &location);
    }
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
            std::vector<Expr> args = index_arg_exprs(stmt_target_expr(stmt).children[1]);
            args.push_back(stmt.value_expr);
            if (const auto signature = dudu_operator_signature_for_args(
                    scope.symbols, "[]=", receiver_type, args,
                    infer_assignment_arg_type_refs(scope, args, &target_location))) {
                check_call_args_ast(scope, name + "[]=", *signature, args, &target_location);
                return {};
            }
            check_declared_index_assignment_operator_if_any(scope, receiver_type, name, args,
                                                            target_location);
            if (class_for_receiver_type(scope.symbols, receiver_type) != nullptr) {
                sema_fail(target_location,
                          "no matching @operator(\"[]=\") for indexed assignment to " + name);
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
            std::vector<Expr> args = index_arg_exprs(stmt_target_expr(stmt).children[1]);
            args.push_back(stmt.value_expr);
            if (const auto signature = dudu_operator_signature_for_args(
                    scope.symbols, "[]=", receiver_type, args,
                    infer_assignment_arg_type_refs(scope, args, &target_location))) {
                check_call_args_ast(scope, indexed_assignment_label(receiver) + "[]=", *signature,
                                    args, &target_location);
                return {};
            }
            check_declared_index_assignment_operator_if_any(
                scope, receiver_type, indexed_assignment_label(receiver), args, target_location);
            if (class_for_receiver_type(scope.symbols, receiver_type) != nullptr) {
                sema_fail(target_location,
                          "no matching @operator(\"[]=\") for indexed assignment to " +
                              indexed_assignment_label(receiver));
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
        reject_const_member_assignment(scope, stmt_target_expr(stmt), target_location);
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
