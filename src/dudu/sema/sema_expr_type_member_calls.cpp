#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_expr_internal.hpp"

namespace dudu {
namespace {

std::optional<TypeRef> receiver_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                              const std::string& callee,
                                              const std::string& method_name,
                                              const std::vector<TypeRef>& method_args,
                                              const SourceLocation* location) {
    const Expr& member = expr_callee(expr).front();
    const Expr& receiver_expr = member.children.front();
    FunctionSignature signature;
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name == "class" &&
        !scope.current_class.empty() &&
        static_method_signature_for_type(
            scope.symbols, named_type_ref(scope.current_class, receiver_expr.location), method_name,
            method_args, signature, location)) {
        check_call_args_ast(scope, callee, signature, expr.children, location);
        return signature_return_type_ref(signature);
    }
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name != "class" &&
        !scope.local_type_refs.contains(receiver_expr.name) &&
        scope.symbols.classes.contains(receiver_expr.name) &&
        static_method_signature_for_type(scope.symbols,
                                         named_type_ref(receiver_expr.name, receiver_expr.location),
                                         method_name, method_args, signature, location)) {
        check_call_args_ast(scope, callee, signature, expr.children, location);
        return signature_return_type_ref(signature);
    }
    const bool bare_nonlocal_receiver =
        receiver_expr.kind == ExprKind::Name && !scope.local_type_refs.contains(receiver_expr.name);
    if (bare_nonlocal_receiver) {
        return std::nullopt;
    }
    const TypeRef receiver_type_ref = infer_expr_type_ast(scope, receiver_expr, location);
    if (!has_type_ref(receiver_type_ref) || type_ref_is_auto(receiver_type_ref)) {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return named_type_ref("auto", expr.location);
    }
    const bool foreign_receiver = foreign_cpp_type_name(scope.symbols, receiver_type_ref);
    if (method_signature_for_type(scope.symbols, receiver_type_ref, method_name, method_args,
                                  signature, foreign_receiver ? nullptr : location)) {
        const std::vector<FunctionSignature> signatures =
            method_signatures_for_type(scope.symbols, receiver_type_ref, method_name, method_args);
        if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
            check_call_args_ast(scope, callee, *match, expr.children, location);
            return signature_return_type_ref(*match);
        }
        check_call_args_ast(scope, callee, signature, expr.children, location);
        return signature_return_type_ref(signature);
    }
    if (foreign_receiver) {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return named_type_ref("auto", expr.location);
    }
    return std::nullopt;
}

} // namespace

std::optional<TypeRef> direct_member_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const std::string& callee,
                                                   const SourceLocation* location) {
    if (!has_expr_callee(expr) || expr_callee(expr).front().kind != ExprKind::Member ||
        expr_callee(expr).front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr_callee(expr).front();
    const Expr& receiver_expr = member.children.front();
    const bool bare_nonlocal_receiver =
        receiver_expr.kind == ExprKind::Name && !scope.local_type_refs.contains(receiver_expr.name);
    const bool static_class_receiver =
        receiver_expr.kind == ExprKind::Name &&
        (receiver_expr.name == "class" || scope.symbols.classes.contains(receiver_expr.name));
    if (!bare_nonlocal_receiver && !static_class_receiver) {
        const TypeRef receiver_type_ref = infer_expr_type_ast(scope, receiver_expr, location);
        const bool foreign_receiver = foreign_cpp_type_name(scope.symbols, receiver_type_ref);
        if (!foreign_receiver) {
            if (const auto inferred = inferred_generic_method_signature_for_type(
                    scope, receiver_type_ref, member.name, expr.children, location)) {
                check_call_args_ast(scope, callee, *inferred, expr.children, location);
                return signature_return_type_ref(*inferred);
            }
        }
    }
    return receiver_call_type_ref(scope, expr, callee, member.name, {}, location);
}

std::optional<TypeRef> direct_member_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location) {
    if (!has_expr_callee(expr) || expr_callee(expr).front().kind != ExprKind::Member) {
        return std::nullopt;
    }
    const std::string callee = expr_label(expr_callee(expr).front());
    return direct_member_call_type_ref(scope, expr, callee, location);
}

std::optional<TypeRef> direct_template_member_call_type_ref(const FunctionScope& scope,
                                                            const Expr& expr,
                                                            const std::string& callee,
                                                            const SourceLocation* location) {
    if (!has_expr_callee(expr) || expr_callee(expr).front().kind != ExprKind::Member ||
        expr_callee(expr).front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr_callee(expr).front();
    return receiver_call_type_ref(scope, expr, callee, member.name, expr_template_type_args(expr),
                                  location);
}

} // namespace dudu
