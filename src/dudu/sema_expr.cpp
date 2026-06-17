#include "dudu/sema_expr.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
[[noreturn]] void sema_expr_fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message, "dudu.sema.expression");
}

TypeRef infer_expr_type_ast(const FunctionScope& scope, const Expr& expr,
                            const SourceLocation* location) {
    const SourceLocation type_location =
        location == nullptr ? node_location(expr.location, expr) : node_location(*location, expr);
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        return parse_type_text("bool", type_location);
    case ExprKind::IntLiteral:
        return parse_type_text("i32", type_location);
    case ExprKind::FloatLiteral:
        return parse_type_text("f64", type_location);
    case ExprKind::StringLiteral:
        return parse_type_text("str", type_location);
    case ExprKind::NoneLiteral:
        return parse_type_text("None", type_location);
    case ExprKind::Name:
        if (const auto local = scope.local_type_refs.find(expr.name);
            local != scope.local_type_refs.end()) {
            return local->second;
        }
        if (const auto local = scope.locals.find(expr.name); local != scope.locals.end()) {
            return parse_type_text(local->second, type_location);
        }
        if (const auto fn = scope.symbols.function_signatures.find(expr.name);
            fn != scope.symbols.function_signatures.end()) {
            return function_type_ref(fn->second, type_location);
        }
        if (const auto value = scope.symbols.native_values.find(expr.name);
            value != scope.symbols.native_values.end()) {
            return parse_type_text(value->second, type_location);
        }
        if (const auto native = scope.symbols.native_function_signatures.find(expr.name);
            native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
            return function_type_ref(native->second.front(), type_location);
        }
        break;
    case ExprKind::TupleLiteral: {
        TypeRef tuple;
        tuple.kind = TypeKind::Template;
        tuple.name = "tuple";
        tuple.location = expr.location;
        tuple.range = expr.range;
        tuple.children.reserve(expr.children.size());
        for (const Expr& child : expr.children) {
            tuple.children.push_back(infer_expr_type_ast(scope, child, location));
        }
        tuple.text = substitute_type_ref_text(tuple, {});
        return tuple;
    }
    case ExprKind::Call:
        if (const auto call_type = direct_call_type_ref(scope, expr, location)) {
            return *call_type;
        }
        break;
    case ExprKind::TemplateCall:
        if (const auto call_type = direct_template_call_type_ref(scope, expr, location)) {
            return *call_type;
        }
        break;
    case ExprKind::Unary:
        if (const auto unary_type = unary_expr_type_ref(scope, expr, location)) {
            return *unary_type;
        }
        break;
    case ExprKind::Binary:
        if (const auto binary_type = binary_expr_type_ref(scope, expr, location)) {
            return *binary_type;
        }
        break;
    case ExprKind::Index:
        if (expr.children.size() == 2 && !missing_expr(expr.children[0]) &&
            !missing_expr(expr.children[1])) {
            const SourceLocation& index_location = location != nullptr ? *location : expr.location;
            const Expr& receiver = expr.children[0];
            if (receiver.kind == ExprKind::Name) {
                if (const auto local = scope.locals.find(receiver.name);
                    local != scope.locals.end()) {
                    if (const auto signature =
                            dudu_operator_signature(scope.symbols, "[]", local->second)) {
                        check_call_args_ast(scope, receiver.name + "[]", *signature,
                                            index_arg_exprs(expr.children[1]), location);
                    }
                }
                return indexed_value_type_ref(scope.symbols, scope.locals, scope.local_type_refs,
                                              index_location, receiver.name, expr.children[1],
                                              "indexed access to unknown local: ");
            }
            const TypeRef receiver_member_type =
                member_expr_type_ref(scope.symbols, scope.locals, scope.local_type_refs, location,
                                     receiver, {}, scope.current_class);
            if (has_type_ref(receiver_member_type)) {
                return indexed_type_ref_from_type(
                    scope.symbols, index_location, receiver_member_type, expr.children[1],
                    display_expr(receiver).empty() ? "indexed expression" : display_expr(receiver));
            }
            const TypeRef receiver_type = infer_expr_type_ast(scope, receiver, location);
            if (has_type_ref(receiver_type)) {
                return indexed_type_ref_from_type(
                    scope.symbols, index_location, receiver_type, expr.children[1],
                    display_expr(receiver).empty() ? "indexed expression" : display_expr(receiver));
            }
        }
        break;
    case ExprKind::Member:
        if (const auto member_type = member_expr_direct_type_ref(scope, expr, location)) {
            return *member_type;
        }
        break;
    case ExprKind::DefExpression:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: def expressions");
        }
        return {};
    case ExprKind::Comprehension:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: comprehensions");
        }
        return {};
    default:
        break;
    }

    const std::string inferred = infer_expr_ast(scope, expr, location);
    return inferred.empty() ? TypeRef{} : parse_type_text(inferred, type_location);
}

std::string infer_expr_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* location) {
    const SourceLocation* use_location =
        location != nullptr
            ? location
            : (expr.range.end.column > expr.range.start.column ? &expr.location : nullptr);
    switch (expr.kind) {
    case ExprKind::Unknown:
        if (trim(expr.text).empty()) {
            return {};
        }
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported expression: " + trim(expr.text));
        }
        return {};
    case ExprKind::BoolLiteral:
        return "bool";
    case ExprKind::IntLiteral:
        return "i32";
    case ExprKind::FloatLiteral:
        return "f64";
    case ExprKind::StringLiteral:
        return "str";
    case ExprKind::NoneLiteral:
        return "None";
    case ExprKind::DefExpression:
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported Python feature: def expressions");
        }
        return {};
    case ExprKind::Comprehension:
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported Python feature: comprehensions");
        }
        return {};
    case ExprKind::Lambda:
        if (use_location != nullptr) {
            sema_expr_fail(
                *use_location,
                "unsupported Python feature: lambda; declare a named function and pass the "
                "function name");
        }
        return {};
    case ExprKind::ListLiteral:
        return "list";
    case ExprKind::DictLiteral:
        return "dict";
    case ExprKind::DictEntry:
        return "auto";
    case ExprKind::NamedArg:
        if (expr.children.size() == 1) {
            return infer_expr_ast(scope, expr.children.front(), use_location);
        }
        return "auto";
    case ExprKind::Slice:
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "slice expression must be used inside an index");
        }
        for (const Expr& child : expr.children) {
            if (!missing_expr(child)) {
                (void)infer_expr_ast(scope, child, use_location);
            }
        }
        return "slice";
    case ExprKind::SetLiteral:
        return "set";
    case ExprKind::TupleLiteral:
        return substitute_type_ref_text(infer_expr_type_ast(scope, expr, use_location), {});
    case ExprKind::Name:
        if (const auto local = scope.locals.find(expr.name); local != scope.locals.end()) {
            return local->second;
        }
        if (const auto fn = scope.symbols.function_signatures.find(expr.name);
            fn != scope.symbols.function_signatures.end()) {
            return function_type(fn->second);
        }
        if (const auto value = scope.symbols.native_values.find(expr.name);
            value != scope.symbols.native_values.end()) {
            return value->second;
        }
        if (const auto native = scope.symbols.native_function_signatures.find(expr.name);
            native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
            return function_type(native->second.front());
        }
        if (is_dudu_all_caps(expr.name)) {
            return "i32";
        }
        if (use_location != nullptr) {
            throw CompileError(*use_location, "unknown identifier: " + expr.name,
                               "dudu.sema.unknown_identifier", expr.name);
        }
        return {};
    case ExprKind::Unary:
        if (const auto type = unary_expr_type_ref(scope, expr, use_location)) {
            return substitute_type_ref_text(*type, {});
        }
        return {};
    case ExprKind::Binary: {
        if (const auto type = binary_expr_type_ref(scope, expr, use_location)) {
            return substitute_type_ref_text(*type, {});
        }
        return {};
    }
    case ExprKind::Member:
        if (const auto type = member_expr_direct_type_ref(scope, expr, use_location)) {
            return substitute_type_ref_text(*type, {});
        }
        return {};
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            if (missing_expr(expr.children[0]) || missing_expr(expr.children[1])) {
                if (use_location != nullptr) {
                    sema_expr_fail(*use_location, "index expression expects receiver and index");
                }
                return {};
            }
            const SourceLocation& index_location =
                use_location != nullptr ? *use_location : expr.location;
            const Expr& receiver = expr.children[0];
            if (receiver.kind == ExprKind::Name) {
                if (const auto local = scope.locals.find(receiver.name);
                    local != scope.locals.end()) {
                    if (const auto signature =
                            dudu_operator_signature(scope.symbols, "[]", local->second)) {
                        check_call_args_ast(scope, receiver.name + "[]", *signature,
                                            index_arg_exprs(expr.children[1]), use_location);
                    }
                }
                return indexed_value_type(scope.symbols, scope.locals, scope.local_type_refs,
                                          index_location, receiver.name, expr.children[1],
                                          "indexed access to unknown local: ");
            }
            if (const std::string receiver_type = member_expr_type(
                    scope.symbols, scope.locals, use_location, receiver, {}, scope.current_class);
                !receiver_type.empty()) {
                return indexed_type_from_type(
                    scope.symbols, index_location, receiver_type, expr.children[1],
                    display_expr(receiver).empty() ? "indexed expression" : display_expr(receiver));
            }
            const std::string receiver_type = infer_expr_ast(scope, receiver, use_location);
            if (!receiver_type.empty()) {
                return indexed_type_from_type(
                    scope.symbols, index_location, receiver_type, expr.children[1],
                    display_expr(receiver).empty() ? "indexed expression" : display_expr(receiver));
            }
        }
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported index expression: " + display_expr(expr));
        }
        return {};
    case ExprKind::Conditional:
        if (use_location != nullptr) {
            sema_expr_fail(
                *use_location,
                "unsupported Python feature: conditional expressions; use an explicit if "
                "statement");
        }
        return {};
    case ExprKind::Await:
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported Python feature: async");
        }
        return {};
    case ExprKind::Yield:
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported Python feature: generators");
        }
        return {};
    case ExprKind::Call:
        return infer_call_ast(scope, expr, use_location);
    case ExprKind::TemplateCall:
        return infer_template_call_ast(scope, expr, use_location);
    case ExprKind::CppEscape:
        return infer_cpp_escape_expr(scope, expr.value, use_location);
    }
    return {};
}

BodyCheckCallbacks expression_body_check_callbacks() {
    return {.infer_expr_type =
                [](const FunctionScope& scope, const Expr& expr, const SourceLocation* location) {
                    return infer_expr_type_ast(scope, expr, location);
                },
            .can_assign =
                [](const FunctionScope& scope, const std::string& expected, const Expr& expr,
                   const std::string& got) { return can_assign_ast(scope, expected, expr, got); },
            .can_assign_type =
                [](const FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                   const std::string& got) { return can_assign_ast(scope, expected, expr, got); },
            .check_call_args =
                [](const FunctionScope& scope, const std::string& callee,
                   const FunctionSignature& signature, const std::vector<Expr>& args,
                   const SourceLocation* location) {
                    check_call_args_ast(scope, callee, signature, args, location);
                }};
}

} // namespace dudu
