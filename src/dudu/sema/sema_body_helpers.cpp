#include "dudu/sema/sema_body_helpers.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/type_compat.hpp"

#include <optional>
#include <sstream>
#include <utility>

namespace dudu {
namespace {

bool numeric_literal_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Unary && expr.op == "-" && expr.children.size() == 1) {
        return numeric_literal_expr(expr.children.front());
    }
    return expr.kind == ExprKind::IntLiteral || expr.kind == ExprKind::FloatLiteral;
}

void check_type_match(FunctionScope& scope, const TypeRef& expected_ref, const Expr& expr,
                      const SourceLocation& location, std::string_view mismatch_label) {
    if (expr.kind == ExprKind::Call && has_expr_callee(expr) &&
        expr_callee(expr).front().kind == ExprKind::Member &&
        expr_callee(expr).front().children.size() == 1) {
        const Expr& member = expr_callee(expr).front();
        const Expr& receiver = member.children.front();
        const bool receiver_is_bare_path =
            receiver.kind == ExprKind::Name && !scope.local_type_refs.contains(receiver.name);
        if (!receiver_is_bare_path) {
            const TypeRef receiver_ref = infer_expr_type_ast(scope, receiver, &location);
            const std::vector<DuduMethodInstantiation> methods =
                dudu_method_instantiations_for_type(scope.symbols, receiver_ref, member.name, {});
            for (const DuduMethodInstantiation& method : methods) {
                if (!matching_signature_ast(scope, {method.signature}, expr.children)) {
                    continue;
                }
                const TypeRef signature_return = signature_return_type_ref(method.signature);
                if (type_assignment_allowed(scope.symbols, expected_ref, signature_return) ||
                    can_assign_ast(scope, expected_ref, expr, signature_return)) {
                    const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, &location);
                    check_call_args_ast(scope, scoped_callee.key, method.signature, expr.children,
                                        &location);
                    if (method.owner != nullptr) {
                        check_instantiated_generic_method_body(
                            scope, *method.owner, *method.method, method.receiver_type,
                            method.receiver_args, method.method_args, location);
                    }
                    return;
                }
            }
            if (const auto method = inferred_dudu_method_instantiation_for_type(
                    scope, receiver_ref, member.name, expr.children,
                    std::optional<TypeRef>{expected_ref}, &location)) {
                const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, &location);
                check_call_args_ast(scope, scoped_callee.key, method->signature, expr.children,
                                    &location);
                if (method->owner != nullptr) {
                    check_instantiated_generic_method_body(
                        scope, *method->owner, *method->method, method->receiver_type,
                        method->receiver_args, method->method_args, location);
                }
                const TypeRef signature_return = signature_return_type_ref(method->signature);
                if (type_assignment_allowed(scope.symbols, expected_ref, signature_return) ||
                    can_assign_ast(scope, expected_ref, expr, signature_return)) {
                    return;
                }
            }
        }
    }
    const TypeRef got_ref = infer_expr_type_ast(scope, expr, &location);
    if (!type_assignment_allowed(scope.symbols, expected_ref, got_ref) &&
        !can_assign_ast(scope, expected_ref, expr, got_ref)) {
        if (!mismatch_label.empty()) {
            const std::string expected_display = substitute_type_ref_text(expected_ref, {});
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            sema_fail(location, std::string(mismatch_label) + ": expected " + expected_display +
                                    ", got " + got_display);
        }
        sema_fail(location, assignment_error(expected_ref, expr, got_ref));
    }
}

} // namespace

bool freestanding_like(const FunctionScope& scope) {
    return scope.target_mode == "freestanding" || scope.target_mode == "embedded";
}

bool is_array_literal(const Expr& expr) {
    return expr.kind == ExprKind::ListLiteral;
}

bool function_has_decorator(const FunctionDecl& fn, std::string_view name) {
    return dudu::has_decorator(fn.decorators, name);
}

void check_type_ref_match(FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                          const SourceLocation& location, std::string_view mismatch_label) {
    if (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) {
        check_type_match(scope, expected, expr, location, mismatch_label);
        return;
    }
    const TypeRef got_ref = infer_expr_type_ast(scope, expr, &location);
    if (!type_assignment_allowed(scope.symbols, expected, got_ref) &&
        !assignment_type_allowed(scope.symbols, expected, expr, got_ref) &&
        !can_assign_ast(scope, expected, expr, got_ref)) {
        if (!mismatch_label.empty()) {
            const std::string expected_display = substitute_type_ref_text(expected, {});
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            sema_fail(location, std::string(mismatch_label) + ": expected " + expected_display +
                                    ", got " + got_display);
        }
        sema_fail(location, assignment_error(expected, expr, got_ref));
    }
}

void check_array_literal_elements(FunctionScope& scope, const TypeRef& element_type,
                                  const Expr& expr, const SourceLocation& location) {
    if (expr.kind != ExprKind::ListLiteral) {
        const TypeRef got_ref = infer_expr_type_ast(scope, expr, &location);
        if (!type_assignment_allowed(scope.symbols, element_type, got_ref) &&
            !can_assign_ast(scope, element_type, expr, got_ref)) {
            const std::string expected_display = substitute_type_ref_text(element_type, {});
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            sema_fail(location,
                      "array literal element expects " + expected_display + ", got " + got_display);
        }
        return;
    }
    for (const Expr& child : expr.children) {
        check_array_literal_elements(scope, element_type, child,
                                     diagnostic_location(location, child));
    }
}

EffectiveVarType effective_var_type(const Stmt& stmt, const ArrayShapeInference& inferred) {
    if (inferred.status == ArrayShapeStatus::Inferred) {
        return {.ref = inferred.type_ref, .inferred = true};
    }
    return {.ref = stmt_type_ref(stmt)};
}

std::string shape_display(const std::vector<size_t>& shape) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << shape[i];
    }
    out << "]";
    return out.str();
}

TypeRef const_reference_type_ref(TypeRef type) {
    TypeRef const_type = wrapped_type_ref(TypeKind::Const, std::move(type));
    const_type.name = "const";
    return wrapped_type_ref(TypeKind::Reference, std::move(const_type));
}

void check_condition_type(FunctionScope& scope, const Stmt& stmt) {
    const SourceLocation& location = diagnostic_location(stmt.location, stmt_condition_expr(stmt));
    const TypeRef got_ref = infer_expr_type_ast(scope, stmt_condition_expr(stmt), &location);
    if (has_type_ref(got_ref) && !type_ref_is_name(got_ref, "bool") && !type_ref_is_auto(got_ref)) {
        if (const auto signature = dudu_operator_signature(scope.symbols, "bool", got_ref);
            signature && signature_param_count(*signature) == 0 &&
            type_ref_is_name(signature_return_type_ref(*signature), "bool")) {
            return;
        }
        const std::string got_display = substitute_type_ref_text(got_ref, {});
        sema_fail(location, "condition must be bool, got " + got_display);
    }
}

std::optional<TypeRef> infer_for_binding_type(FunctionScope& scope, const Stmt& stmt) {
    if (!has_stmt_iterable_expr(stmt)) {
        return std::nullopt;
    }
    const SourceLocation& location = diagnostic_location(stmt.location, stmt_iterable_expr(stmt));
    if (direct_callee_name(stmt_iterable_expr(stmt)) == "range") {
        TypeRef inferred;
        const std::vector<Expr>& args = stmt_iterable_expr(stmt).children;
        for (const Expr& arg : args) {
            const TypeRef arg_type = infer_expr_type_ast(scope, arg, &location);
            if (!numeric_literal_expr(arg) && has_type_ref(arg_type)) {
                inferred = arg_type;
            }
        }
        if (has_type_ref(inferred)) {
            return inferred;
        }
        return named_type_ref("i32", location);
    }
    if (stmt_iterable_expr(stmt).kind == ExprKind::Name) {
        const std::optional<TypeRef> element =
            iterable_value_type_ref(scope.local_type_refs, stmt_iterable_expr(stmt).name);
        if (element) {
            return *element;
        }
    }
    const TypeRef iterable_type = infer_expr_type_ast(scope, stmt_iterable_expr(stmt), &location);
    if (!has_type_ref(iterable_type)) {
        return std::nullopt;
    }
    if (const auto element = iterable_type_ref_from_type(iterable_type)) {
        return *element;
    }
    return std::nullopt;
}

} // namespace dudu
