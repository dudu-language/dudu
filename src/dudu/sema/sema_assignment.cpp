#include "dudu/sema/sema_assignment.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
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
#include <set>
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
        if (arg.kind == ExprKind::Ellipsis) {
            out.push_back(named_type_ref("ellipsis", arg.location));
            continue;
        }
        if (arg.kind == ExprKind::NewAxis) {
            out.push_back(named_type_ref("new_axis", arg.location));
            continue;
        }
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

TypeRef assignment_template_receiver_type(const Symbols& symbols, TypeRef type) {
    type = resolved_assignment_type(symbols, std::move(type));
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const ||
            type.kind == TypeKind::Pointer || type.kind == TypeKind::Storage ||
            type.kind == TypeKind::Shared || type.kind == TypeKind::Device ||
            type.kind == TypeKind::Volatile || type.kind == TypeKind::Atomic) &&
           type.children.size() == 1) {
        type = resolved_assignment_type(symbols, type.children.front());
    }
    return type;
}

bool is_map_like_assignment_receiver(const TypeRef& type) {
    static const std::set<std::string_view> names = {"dict", "std.map", "std::map",
                                                     "std.unordered_map", "std::unordered_map"};
    return type.kind == TypeKind::Template && type.children.size() == 2 &&
           names.contains(type.name);
}

bool index_args_include_view_marker(const std::vector<Expr>& args) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        const Expr& arg = args[i];
        if (arg.kind == ExprKind::Slice || arg.kind == ExprKind::Ellipsis ||
            arg.kind == ExprKind::NewAxis) {
            return true;
        }
    }
    return false;
}

std::optional<TypeRef> map_like_index_assignment_type(const Symbols& symbols,
                                                      const TypeRef& receiver_type,
                                                      const std::vector<Expr>& args,
                                                      const SourceLocation& location,
                                                      const std::string& label) {
    const TypeRef type = assignment_template_receiver_type(symbols, receiver_type);
    if (!is_map_like_assignment_receiver(type)) {
        return std::nullopt;
    }
    if (args.size() != 2 || index_args_include_view_marker(args)) {
        sema_fail(location, "map indexing requires exactly one key: " + label);
    }
    return type.children[1];
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

void check_declared_index_assignment_operator_if_any(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& op,
    const std::string& label, const std::vector<Expr>& args, const SourceLocation& location) {
    if (const auto signature = dudu_operator_signature(scope.symbols, op, receiver_type)) {
        (void)signature;
        sema_fail(location, dudu_operator_no_match_message_for_args(
                                scope.symbols, op, receiver_type, args,
                                infer_assignment_arg_type_refs(scope, args, &location),
                                "indexed assignment", label));
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
            const std::string type_display = trim_string(substitute_type_ref_text(type, {}));
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
            check_index_arg_exprs(args, &target_location);
            args.push_back(stmt.value_expr);
            const std::vector<TypeRef> arg_types =
                infer_assignment_arg_type_refs(scope, args, &target_location);
            if (const auto signature = dudu_operator_signature_for_args(
                    scope.symbols, "[]=", receiver_type, args, arg_types)) {
                check_call_args_ast(scope, name + "[]=", *signature, args, &target_location);
                check_instantiated_dudu_operator_body(scope, "[]=", receiver_type, args, arg_types,
                                                      target_location);
                return {};
            }
            check_declared_index_assignment_operator_if_any(scope, receiver_type, "[]=", name, args,
                                                            target_location);
            if (const auto map_target = map_like_index_assignment_type(
                    scope.symbols, receiver_type, args, target_location, name)) {
                return *map_target;
            }
            if (class_for_receiver_type(scope.symbols, receiver_type) != nullptr) {
                sema_fail(target_location,
                          dudu_operator_no_match_message_for_args(
                              scope.symbols, "[]=", receiver_type, args,
                              infer_assignment_arg_type_refs(scope, args, &target_location),
                              "indexed assignment", name));
            }
        }
        return indexed_value_type_ref(scope.symbols, scope.local_type_refs, target_location, name,
                                      stmt_target_expr(stmt).children[1],
                                      "indexed assignment to unknown local: ");
    }
    if (stmt_target_expr(stmt).kind == ExprKind::Index &&
        stmt_target_expr(stmt).children.size() == 2) {
        const Expr& receiver = stmt_target_expr(stmt).children[0];
        const IndexOperatorTarget target = index_operator_target(receiver);
        const Expr& hook_receiver = *target.receiver;
        const TypeRef receiver_type =
            hook_receiver.kind == ExprKind::Member
                ? member_expr_type_ref(scope.symbols, scope.local_type_refs, &target_location,
                                       hook_receiver, {}, scope.current_class)
                : TypeRef{};
        if (has_type_ref(receiver_type)) {
            std::vector<Expr> args = index_arg_exprs(stmt_target_expr(stmt).children[1]);
            check_index_arg_exprs(args, &target_location);
            args.push_back(stmt.value_expr);
            const std::vector<TypeRef> arg_types =
                infer_assignment_arg_type_refs(scope, args, &target_location);
            if (const auto signature = dudu_operator_signature_for_args(
                    scope.symbols, target.write_operator, receiver_type, args, arg_types)) {
                check_call_args_ast(scope,
                                    indexed_assignment_label(receiver) + target.write_operator,
                                    *signature, args, &target_location);
                check_instantiated_dudu_operator_body(scope, target.write_operator, receiver_type,
                                                      args, arg_types, target_location);
                return {};
            }
            check_declared_index_assignment_operator_if_any(
                scope, receiver_type, target.write_operator, indexed_assignment_label(receiver),
                args, target_location);
            if (const auto map_target = map_like_index_assignment_type(
                    scope.symbols, receiver_type, args, target_location,
                    indexed_assignment_label(receiver))) {
                return *map_target;
            }
            if (class_for_receiver_type(scope.symbols, receiver_type) != nullptr) {
                sema_fail(target_location,
                          dudu_operator_no_match_message_for_args(
                              scope.symbols, target.write_operator, receiver_type, args,
                              infer_assignment_arg_type_refs(scope, args, &target_location),
                              "indexed assignment", indexed_assignment_label(receiver)));
            }
            return indexed_type_ref_from_type(scope.symbols, target_location, receiver_type,
                                              stmt_target_expr(stmt).children[1],
                                              indexed_assignment_label(receiver));
        }
        const TypeRef inferred_receiver_type =
            infer_expr_type_ast(scope, hook_receiver, &target_location);
        if (has_type_ref(inferred_receiver_type)) {
            std::vector<Expr> args = index_arg_exprs(stmt_target_expr(stmt).children[1]);
            check_index_arg_exprs(args, &target_location);
            args.push_back(stmt.value_expr);
            const std::vector<TypeRef> arg_types =
                infer_assignment_arg_type_refs(scope, args, &target_location);
            if (const auto signature =
                    dudu_operator_signature_for_args(scope.symbols, target.write_operator,
                                                     inferred_receiver_type, args, arg_types)) {
                check_call_args_ast(scope,
                                    indexed_assignment_label(receiver) + target.write_operator,
                                    *signature, args, &target_location);
                check_instantiated_dudu_operator_body(scope, target.write_operator,
                                                      inferred_receiver_type, args, arg_types,
                                                      target_location);
                return {};
            }
            check_declared_index_assignment_operator_if_any(
                scope, inferred_receiver_type, target.write_operator,
                indexed_assignment_label(receiver), args, target_location);
            if (const auto map_target = map_like_index_assignment_type(
                    scope.symbols, inferred_receiver_type, args, target_location,
                    indexed_assignment_label(receiver))) {
                return *map_target;
            }
            if (class_for_receiver_type(scope.symbols, inferred_receiver_type) != nullptr) {
                sema_fail(target_location,
                          dudu_operator_no_match_message_for_args(
                              scope.symbols, target.write_operator, inferred_receiver_type, args,
                              infer_assignment_arg_type_refs(scope, args, &target_location),
                              "indexed assignment", indexed_assignment_label(receiver)));
            }
            return indexed_type_ref_from_type(
                scope.symbols, target_location, inferred_receiver_type,
                stmt_target_expr(stmt).children[1], indexed_assignment_label(receiver));
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

TypeRef compound_assignment_target_type_ref(FunctionScope& scope, const Stmt& stmt) {
    const Expr& target = stmt_target_expr(stmt);
    const SourceLocation& target_location = diagnostic_location(stmt.location, target);
    if (target.kind != ExprKind::Index || target.children.size() != 2) {
        return assignment_target_type_ref(scope, stmt);
    }

    const Expr& receiver = target.children[0];
    const IndexOperatorTarget index_target = index_operator_target(receiver);
    const Expr& hook_receiver = *index_target.receiver;
    TypeRef receiver_type;
    if (hook_receiver.kind == ExprKind::Name &&
        scope.local_type_refs.contains(hook_receiver.name)) {
        receiver_type = local_type_ref(scope, hook_receiver.name, target_location);
    } else {
        receiver_type =
            hook_receiver.kind == ExprKind::Member
                ? member_expr_type_ref(scope.symbols, scope.local_type_refs, &target_location,
                                       hook_receiver, {}, scope.current_class)
                : TypeRef{};
        if (!has_type_ref(receiver_type)) {
            receiver_type = infer_expr_type_ast(scope, hook_receiver, &target_location);
        }
    }
    if (!has_type_ref(receiver_type) ||
        class_for_receiver_type(scope.symbols, receiver_type) == nullptr) {
        return assignment_target_type_ref(scope, stmt);
    }

    const TypeRef indexed_type = infer_expr_type_ast(scope, target, &target_location);
    if (!has_type_ref(indexed_type)) {
        return indexed_type;
    }
    const Expr compound_value = compound_assignment_value_expr(stmt);
    const TypeRef compound_type = infer_expr_type_ast(scope, compound_value, &target_location);
    if (!has_type_ref(compound_type)) {
        return compound_type;
    }

    std::vector<Expr> args = index_arg_exprs(target.children[1]);
    check_index_arg_exprs(args, &target_location);
    std::vector<TypeRef> arg_types = infer_assignment_arg_type_refs(scope, args, &target_location);
    args.push_back(compound_value);
    arg_types.push_back(compound_type);
    if (!dudu_operator_signature_for_arg_types(scope.symbols, index_target.write_operator,
                                               receiver_type, arg_types)) {
        sema_fail(target_location,
                  "compound indexed assignment requires @operator(\"" +
                      index_target.write_operator +
                      "\") accepting the indexed value type; use an explicit read/modify value "
                      "and indexed assignment");
    }
    check_instantiated_dudu_operator_body(scope, index_target.write_operator, receiver_type, args,
                                          arg_types, target_location);
    return compound_type;
}

} // namespace dudu
