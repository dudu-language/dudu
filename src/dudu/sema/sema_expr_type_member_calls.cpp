#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_expr_internal.hpp"

namespace dudu {
namespace {

bool typed_value_receiver(const FunctionScope& scope, const Expr& expr) {
    return expr.kind == ExprKind::Name &&
           (scope.local_type_refs.contains(expr.name) ||
            scope.symbols.native_value_type_refs.contains(expr.name));
}

TypeRef check_dudu_method_instantiation(const FunctionScope& scope, const Expr& expr,
                                        const std::string& callee,
                                        const DuduMethodInstantiation& method,
                                        const SourceLocation* location) {
    check_call_args_ast(scope, callee, method.signature, expr.children, location);
    if (location != nullptr && method.owner != nullptr && method.method != nullptr) {
        check_instantiated_generic_method_body(scope, *method.owner, *method.method,
                                               method.receiver_type, method.receiver_args,
                                               method.method_args, *location);
    }
    return signature_return_type_ref(method.signature);
}

std::optional<TypeRef>
static_dudu_call_type_ref(const FunctionScope& scope, const Expr& expr, const std::string& callee,
                          const TypeRef& receiver_type, const std::string& method_name,
                          const std::vector<TypeRef>& method_args, const SourceLocation* location) {
    const std::vector<DuduMethodInstantiation> methods =
        dudu_method_instantiations_for_type(scope.symbols, receiver_type, method_name, method_args);
    for (const DuduMethodInstantiation& method : methods) {
        const bool is_static =
            method.method != nullptr &&
            (method.method->params.empty() || method.method->params.front().name != "self");
        if (is_static && matching_signature_ast(scope, {method.signature}, expr.children)) {
            return check_dudu_method_instantiation(scope, expr, callee, method, location);
        }
    }
    return std::nullopt;
}

std::optional<TypeRef> receiver_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                              const std::string& callee,
                                              const std::string& method_name,
                                              const std::vector<TypeRef>& method_args,
                                              const SourceLocation* location) {
    const Expr& member = expr_callee(expr).front();
    const Expr& receiver_expr = member.children.front();
    FunctionSignature signature;
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name == "class" &&
        !scope.current_class.empty()) {
        const TypeRef current_type = named_type_ref(scope.current_class, receiver_expr.location);
        if (const auto dudu = static_dudu_call_type_ref(scope, expr, callee, current_type,
                                                        method_name, method_args, location)) {
            return dudu;
        }
        if (static_method_signature_for_type(scope.symbols, current_type, method_name, method_args,
                                             signature, location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
    }
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name != "class" &&
        !scope.local_type_refs.contains(receiver_expr.name) &&
        scope.symbols.classes.contains(receiver_expr.name)) {
        const TypeRef static_type = named_type_ref(receiver_expr.name, receiver_expr.location);
        if (const auto dudu = static_dudu_call_type_ref(scope, expr, callee, static_type,
                                                        method_name, method_args, location)) {
            return dudu;
        }
        if (static_method_signature_for_type(scope.symbols, static_type, method_name, method_args,
                                             signature, location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
    }
    const bool bare_nonlocal_receiver =
        receiver_expr.kind == ExprKind::Name && !typed_value_receiver(scope, receiver_expr);
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
    const std::vector<DuduMethodInstantiation> dudu_methods = dudu_method_instantiations_for_type(
        scope.symbols, receiver_type_ref, method_name, method_args);
    if (!dudu_methods.empty()) {
        for (const DuduMethodInstantiation& method : dudu_methods) {
            if (matching_signature_ast(scope, {method.signature}, expr.children)) {
                return check_dudu_method_instantiation(scope, expr, callee, method, location);
            }
        }
        check_call_args_ast(scope, callee, dudu_methods.front().signature, expr.children, location);
        return signature_return_type_ref(dudu_methods.front().signature);
    }
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
        receiver_expr.kind == ExprKind::Name && !typed_value_receiver(scope, receiver_expr);
    const bool static_class_receiver =
        receiver_expr.kind == ExprKind::Name &&
        (receiver_expr.name == "class" || scope.symbols.classes.contains(receiver_expr.name));
    if (!bare_nonlocal_receiver && !static_class_receiver) {
        const TypeRef receiver_type_ref = infer_expr_type_ast(scope, receiver_expr, location);
        const bool foreign_receiver = foreign_cpp_type_name(scope.symbols, receiver_type_ref);
        if (!foreign_receiver) {
            if (const auto inferred = inferred_dudu_method_instantiation_for_type(
                    scope, receiver_type_ref, member.name, expr.children, location)) {
                return check_dudu_method_instantiation(scope, expr, callee, *inferred, location);
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
