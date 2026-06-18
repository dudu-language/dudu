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
    case ExprKind::Missing:
        return {};
    case ExprKind::Unknown:
        if (trim(expr.text).empty()) {
            return {};
        }
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported expression: " + trim(expr.text));
        }
        return {};
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
    case ExprKind::ListLiteral:
        return parse_type_text("list", type_location);
    case ExprKind::DictLiteral:
        return parse_type_text("dict", type_location);
    case ExprKind::DictEntry:
        return parse_type_text("auto", type_location);
    case ExprKind::NamedArg:
        return expr.children.size() == 1
                   ? infer_expr_type_ast(scope, expr.children.front(), location)
                   : parse_type_text("auto", type_location);
    case ExprKind::Slice:
        if (location != nullptr) {
            sema_expr_fail(*location, "slice expression must be used inside an index");
        }
        for (const Expr& child : expr.children) {
            if (!missing_expr(child)) {
                check_expr_ast(scope, child, location);
            }
        }
        return parse_type_text("slice", type_location);
    case ExprKind::SetLiteral:
        return parse_type_text("set", type_location);
    case ExprKind::Name:
        if (const TypeRef local = local_type_ref(scope, expr.name, type_location);
            has_type_ref(local)) {
            return local;
        }
        if (const auto fn = scope.symbols.function_signatures.find(expr.name);
            fn != scope.symbols.function_signatures.end()) {
            return function_type_ref(fn->second, type_location);
        }
        if (const auto value = scope.symbols.native_value_type_refs.find(expr.name);
            value != scope.symbols.native_value_type_refs.end()) {
            return value->second;
        }
        if (const auto value = scope.symbols.native_values.find(expr.name);
            value != scope.symbols.native_values.end()) {
            return parse_type_text(value->second, type_location);
        }
        if (const auto native = scope.symbols.native_function_signatures.find(expr.name);
            native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
            return function_type_ref(native->second.front(), type_location);
        }
        if (is_dudu_all_caps(expr.name)) {
            return parse_type_text("i32", type_location);
        }
        if (location != nullptr) {
            throw CompileError(*location, "unknown identifier: " + expr.name,
                               "dudu.sema.unknown_identifier", expr.name);
        }
        return {};
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
        if (location != nullptr) {
            const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
            const std::string& callee = scoped_callee.key;
            if (!expr.callee.empty() && expr.callee.front().kind != ExprKind::Name &&
                expr.callee.front().kind != ExprKind::Member) {
                sema_expr_fail(*location, "unsupported call expression: " + callee);
            }
            sema_expr_fail(*location, "unknown function: " + callee);
        }
        return {};
    case ExprKind::TemplateCall:
        if (const auto call_type = direct_template_call_type_ref(scope, expr, location)) {
            return *call_type;
        }
        if (location != nullptr) {
            const std::string callee = template_call_callee(scope, expr, location);
            const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
            const std::string& callee_base = scoped_callee.key;
            if (callee_base.find('.') == std::string::npos && is_plain_identifier(callee_base) &&
                !known_type(scope.symbols, callee_base)) {
                sema_expr_fail(*location, "unknown function: " + callee);
            }
            if (callee_base.rfind('.') != std::string::npos) {
                sema_expr_fail(*location, "unknown function: " + callee);
            }
            if (!expr.callee.empty() && expr.callee.front().kind != ExprKind::Name &&
                expr.callee.front().kind != ExprKind::Member) {
                sema_expr_fail(*location, "unsupported template call expression: " + callee_base);
            }
            sema_expr_fail(*location, "unknown template call: " + callee);
        }
        return {};
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
                if (scope.local_type_refs.contains(receiver.name)) {
                    const TypeRef receiver_type =
                        local_type_ref(scope, receiver.name, index_location);
                    const std::string receiver_type_text =
                        substitute_type_ref_text(receiver_type, {});
                    if (const auto signature =
                            dudu_operator_signature(scope.symbols, "[]", receiver_type_text)) {
                        check_call_args_ast(scope, receiver.name + "[]", *signature,
                                            index_arg_exprs(expr.children[1]), location);
                    }
                }
                return indexed_value_type_ref(scope.symbols, scope.local_type_refs, index_location,
                                              receiver.name, expr.children[1],
                                              "indexed access to unknown local: ");
            }
            const TypeRef receiver_member_type = member_expr_type_ref(
                scope.symbols, scope.local_type_refs, location, receiver, {}, scope.current_class);
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
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported index expression: " + display_expr(expr));
        }
        return {};
    case ExprKind::Member:
        if (const auto member_type = member_expr_direct_type_ref(scope, expr, location)) {
            return *member_type;
        }
        return {};
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
    case ExprKind::Lambda:
        if (location != nullptr) {
            sema_expr_fail(
                *location,
                "unsupported Python feature: lambda; declare a named function and pass the "
                "function name");
        }
        return {};
    case ExprKind::Conditional:
        if (location != nullptr) {
            sema_expr_fail(
                *location,
                "unsupported Python feature: conditional expressions; use an explicit if "
                "statement");
        }
        return {};
    case ExprKind::Await:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: async");
        }
        return {};
    case ExprKind::Yield:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: generators");
        }
        return {};
    case ExprKind::CppEscape:
        if (const std::string inferred = infer_cpp_escape_expr(scope, expr.value, location);
            !inferred.empty()) {
            return parse_type_text(inferred, type_location);
        }
        return {};
    }
    return {};
}

void check_expr_ast(const FunctionScope& scope, const Expr& expr, const SourceLocation* location) {
    (void)infer_expr_type_ast(scope, expr, location);
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
                   const TypeRef& got) { return can_assign_ast(scope, expected, expr, got); },
            .check_call_args =
                [](const FunctionScope& scope, const std::string& callee,
                   const FunctionSignature& signature, const std::vector<Expr>& args,
                   const SourceLocation* location) {
                    check_call_args_ast(scope, callee, signature, args, location);
                }};
}

} // namespace dudu
