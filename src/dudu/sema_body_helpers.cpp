#include "dudu/sema_body_helpers.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/type_compat.hpp"

#include <optional>
#include <sstream>
#include <utility>

namespace dudu {
namespace {

bool type_ref_is_name(const TypeRef& type, std::string_view name) {
    return type.kind == TypeKind::Named && type_ref_head_name(type) == name;
}

bool callback_can_assign_type(const BodyCheckCallbacks& callbacks, const FunctionScope& scope,
                              const TypeRef& expected, const Expr& expr, const std::string& got) {
    if (callbacks.can_assign_type) {
        return callbacks.can_assign_type(scope, expected, expr, got);
    }
    return callbacks.can_assign(scope, substitute_type_ref_text(expected, {}), expr, got);
}

void check_type_match(FunctionScope& scope, const TypeRef& expected_ref, const Expr& expr,
                      const SourceLocation& location, const BodyCheckCallbacks& callbacks,
                      std::string_view mismatch_label) {
    const std::string expected = substitute_type_ref_text(expected_ref, {});
    if (expr.kind == ExprKind::Call && !expr.callee.empty() &&
        expr.callee.front().kind == ExprKind::Member && expr.callee.front().children.size() == 1) {
        const Expr& member = expr.callee.front();
        const Expr& receiver = member.children.front();
        const bool receiver_is_bare_path =
            receiver.kind == ExprKind::Name && !scope.locals.contains(receiver.name);
        if (!receiver_is_bare_path) {
            const TypeRef receiver_ref = callbacks.infer_expr_type(scope, receiver, &location);
            const std::string receiver_type = substitute_type_ref_text(receiver_ref, {});
            if (const auto signature = inferred_generic_method_signature_for_type(
                    scope, receiver_type, member.name, expr.children, expected, &location,
                    {.infer_expr_type =
                         [&](const FunctionScope& nested, const Expr& arg,
                             const SourceLocation* arg_location) {
                             return callbacks.infer_expr_type(nested, arg, arg_location);
                         },
                     .can_assign =
                         [&](const FunctionScope& nested, const std::string& nested_expected,
                             const Expr& value, const std::string& got) {
                             return callbacks.can_assign(nested, nested_expected, value, got);
                         }})) {
                const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, &location);
                callbacks.check_call_args(scope, scoped_callee.key, *signature, expr.children,
                                          &location);
                const TypeRef signature_return = signature_return_type_ref(*signature);
                const std::string signature_return_text =
                    substitute_type_ref_text(signature_return, {});
                if (type_assignment_allowed(expected_ref, signature_return) ||
                    callback_can_assign_type(callbacks, scope, expected_ref, expr,
                                             signature_return_text)) {
                    return;
                }
            }
        }
    }
    const TypeRef got_ref = callbacks.infer_expr_type(scope, expr, &location);
    const std::string got = substitute_type_ref_text(got_ref, {});
    if (!type_assignment_allowed(expected_ref, got_ref) &&
        !callback_can_assign_type(callbacks, scope, expected_ref, expr, got)) {
        if (!mismatch_label.empty()) {
            sema_fail(location,
                      std::string(mismatch_label) + ": expected " + expected + ", got " + got);
        }
        sema_fail(location, assignment_error(expected, expr, got));
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

bool type_ref_is_void(const TypeRef& type) {
    return type_ref_is_name(type, "void");
}

void check_type_ref_match(FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                          const SourceLocation& location, const BodyCheckCallbacks& callbacks,
                          std::string_view mismatch_label) {
    const std::string expected_text = substitute_type_ref_text(expected, {});
    if (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) {
        check_type_match(scope, expected, expr, location, callbacks, mismatch_label);
        return;
    }
    const TypeRef got_ref = callbacks.infer_expr_type(scope, expr, &location);
    const std::string got = substitute_type_ref_text(got_ref, {});
    if (!type_assignment_allowed(expected, got_ref) &&
        !assignment_type_allowed(expected, expr, got_ref) &&
        !callback_can_assign_type(callbacks, scope, expected, expr, got)) {
        if (!mismatch_label.empty()) {
            sema_fail(location,
                      std::string(mismatch_label) + ": expected " + expected_text + ", got " + got);
        }
        sema_fail(location, assignment_error(expected, expr, got));
    }
}

void check_array_literal_elements(FunctionScope& scope, const TypeRef& element_type,
                                  const Expr& expr, const SourceLocation& location,
                                  const BodyCheckCallbacks& callbacks) {
    if (expr.kind != ExprKind::ListLiteral) {
        const TypeRef got_ref = callbacks.infer_expr_type(scope, expr, &location);
        const std::string expected_text = substitute_type_ref_text(element_type, {});
        const std::string got = substitute_type_ref_text(got_ref, {});
        if (!type_assignment_allowed(element_type, got_ref) &&
            !callbacks.can_assign(scope, expected_text, expr, got)) {
            sema_fail(location, "array literal element expects " + expected_text + ", got " + got);
        }
        return;
    }
    for (const Expr& child : expr.children) {
        check_array_literal_elements(scope, element_type, child, node_location(location, child),
                                     callbacks);
    }
}

EffectiveVarType effective_var_type(const Stmt& stmt, const ArrayShapeInference& inferred) {
    if (inferred.status == ArrayShapeStatus::Inferred) {
        return {.text = inferred.type, .ref = inferred.type_ref, .inferred = true};
    }
    return {.text = substitute_type_ref_text(stmt.type_ref, {}), .ref = stmt.type_ref};
}

std::string shape_text(const std::vector<size_t>& shape) {
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
    TypeRef const_type;
    const_type.kind = TypeKind::Const;
    const_type.text = "const[" + substitute_type_ref_text(type, {}) + "]";
    const_type.name = "const";
    const_type.children.push_back(std::move(type));

    TypeRef ref_type;
    ref_type.kind = TypeKind::Reference;
    ref_type.text = "&" + const_type.text;
    ref_type.children.push_back(std::move(const_type));
    return ref_type;
}

void check_condition_type(FunctionScope& scope, const Stmt& stmt,
                          const BodyCheckCallbacks& callbacks) {
    const SourceLocation& location = node_location(stmt.location, stmt.condition_expr);
    const TypeRef got_ref = callbacks.infer_expr_type(scope, stmt.condition_expr, &location);
    const std::string got = substitute_type_ref_text(got_ref, {});
    if (!got.empty() && got != "bool" && got != "auto") {
        if (const auto signature = dudu_operator_signature(scope.symbols, "bool", got);
            signature && signature->params.empty() &&
            type_ref_is_name(signature_return_type_ref(*signature), "bool")) {
            return;
        }
        sema_fail(location, "condition must be bool, got " + got);
    }
}

std::optional<TypeRef> infer_for_binding_type(FunctionScope& scope, const Stmt& stmt,
                                              const BodyCheckCallbacks& callbacks) {
    if (!sema_has_expr(stmt.iterable_expr)) {
        return std::nullopt;
    }
    const SourceLocation& location = node_location(stmt.location, stmt.iterable_expr);
    if (direct_callee_name(stmt.iterable_expr) == "range") {
        for (const Expr& arg : stmt.iterable_expr.children) {
            (void)callbacks.infer_expr_type(scope, arg, &location);
        }
        return parse_type_text("i32", location);
    }
    if (stmt.iterable_expr.kind == ExprKind::Name) {
        const std::optional<TypeRef> element = iterable_value_type_ref(
            scope.symbols, scope.locals, scope.local_type_refs, stmt.iterable_expr.name);
        if (element) {
            return *element;
        }
    }
    const TypeRef iterable_type = callbacks.infer_expr_type(scope, stmt.iterable_expr, &location);
    if (!has_type_ref(iterable_type)) {
        return std::nullopt;
    }
    if (const auto element = iterable_type_ref_from_type(iterable_type)) {
        return *element;
    }
    return std::nullopt;
}

} // namespace dudu
