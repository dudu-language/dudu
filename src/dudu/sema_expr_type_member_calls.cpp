#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

TypeRef named_type_ref(std::string name, SourceLocation location) {
    TypeRef type;
    type.kind = TypeKind::Named;
    type.name = std::move(name);
    type.location = location;
    type.text = type.name;
    return type;
}

std::optional<TypeRef> receiver_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                              const std::string& callee,
                                              const std::string& method_name,
                                              const SourceLocation* location) {
    const Expr& member = expr.callee.front();
    const Expr& receiver_expr = member.children.front();
    FunctionSignature signature;
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name == "class" &&
        !scope.current_class.empty() &&
        static_method_signature_for_type(scope.symbols, scope.current_class, method_name, signature,
                                         location)) {
        check_call_args_ast(scope, callee, signature, expr.children, location);
        return signature_return_type_ref(signature);
    }
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name != "class" &&
        !scope.local_type_refs.contains(receiver_expr.name) &&
        scope.symbols.classes.contains(receiver_expr.name) &&
        static_method_signature_for_type(scope.symbols, receiver_expr.name, method_name, signature,
                                         location)) {
        check_call_args_ast(scope, callee, signature, expr.children, location);
        return signature_return_type_ref(signature);
    }
    const bool bare_nonlocal_receiver =
        receiver_expr.kind == ExprKind::Name && !scope.local_type_refs.contains(receiver_expr.name);
    if (bare_nonlocal_receiver) {
        return std::nullopt;
    }
    const TypeRef receiver_type_ref = infer_expr_type_ast(scope, receiver_expr, location);
    const std::string receiver_type = substitute_type_ref_text(receiver_type_ref, {});
    if (receiver_type.empty() || receiver_type == "auto") {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return named_type_ref("auto", expr.location);
    }
    const bool foreign_receiver =
        foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type));
    if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                  foreign_receiver ? nullptr : location)) {
        const std::vector<FunctionSignature> signatures =
            method_signatures_for_type(scope.symbols, receiver_type, method_name);
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
    if (expr.callee.empty() || expr.callee.front().kind != ExprKind::Member ||
        expr.callee.front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr.callee.front();
    const Expr& receiver_expr = member.children.front();
    const bool bare_nonlocal_receiver =
        receiver_expr.kind == ExprKind::Name && !scope.local_type_refs.contains(receiver_expr.name);
    const bool static_class_receiver =
        receiver_expr.kind == ExprKind::Name &&
        (receiver_expr.name == "class" || scope.symbols.classes.contains(receiver_expr.name));
    if (!bare_nonlocal_receiver && !static_class_receiver) {
        const TypeRef receiver_type_ref = infer_expr_type_ast(scope, receiver_expr, location);
        const std::string receiver_type = substitute_type_ref_text(receiver_type_ref, {});
        const bool foreign_receiver =
            foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type));
        if (!foreign_receiver) {
            if (const auto inferred = inferred_generic_method_signature_for_type(
                    scope, receiver_type, member.name, expr.children, location,
                    {.infer_expr_type =
                         [](const FunctionScope& nested, const Expr& arg,
                            const SourceLocation* arg_location) {
                             return infer_expr_type_ast(nested, arg, arg_location);
                         },
                     .can_assign =
                         [](const FunctionScope& nested, const std::string& expected,
                            const Expr& value, const std::string& got) {
                             return can_assign_ast(nested, expected, value, got);
                         }})) {
                check_call_args_ast(scope, callee, *inferred, expr.children, location);
                return signature_return_type_ref(*inferred);
            }
        }
    }
    return receiver_call_type_ref(scope, expr, callee, member.name, location);
}

std::optional<TypeRef> direct_template_member_call_type_ref(const FunctionScope& scope,
                                                            const Expr& expr,
                                                            const std::string& callee,
                                                            const SourceLocation* location) {
    if (expr.callee.empty() || expr.callee.front().kind != ExprKind::Member ||
        expr.callee.front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr.callee.front();
    const std::string method_name = member.name + "[" + template_args_lookup_text(expr) + "]";
    return receiver_call_type_ref(scope, expr, callee, method_name, location);
}

} // namespace dudu
