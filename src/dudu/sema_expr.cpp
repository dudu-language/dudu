#include "dudu/sema_expr.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

bool type_ref_is_name(const TypeRef& type, std::string_view name) {
    return type.kind == TypeKind::Named && type_ref_head_name(type) == name;
}

} // namespace

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
            if (const std::string receiver_type = member_expr_type(
                    scope.symbols, scope.locals, location, receiver, {}, scope.current_class);
                !receiver_type.empty()) {
                return indexed_type_ref_from_type(
                    scope.symbols, index_location, receiver_type, expr.children[1],
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
    case ExprKind::TupleLiteral: {
        std::ostringstream out;
        out << "tuple[";
        for (size_t i = 0; i < expr.children.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << infer_expr_ast(scope, expr.children[i], use_location);
        }
        out << "]";
        return out.str();
    }
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
        if (expr.children.empty() || missing_expr(expr.children.front())) {
            if (use_location != nullptr) {
                sema_expr_fail(*use_location, "operator " + expr.op + " expects an operand");
            }
            return {};
        }
        if (expr.op == "not") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            if (use_location != nullptr && !got.empty() && got != "bool" && got != "auto") {
                sema_expr_fail(*use_location, "not expects bool, got " + got);
            }
            return "bool";
        }
        if (expr.op == "-") {
            return infer_expr_ast(scope, expr.children.front(), use_location);
        }
        if (expr.op == "~") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            if (use_location != nullptr && !got.empty() && got != "auto" && !is_integer_type(got)) {
                sema_expr_fail(*use_location, "~ expects integer, got " + got);
            }
            return got;
        }
        if (expr.op == "*") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            const std::string type = trim(got);
            if (const auto inner = unary_type_child_text(type, TypeKind::Pointer)) {
                return *inner;
            }
            if (use_location != nullptr && !type.empty() && type != "auto") {
                sema_expr_fail(*use_location, "cannot dereference non-pointer: " + type);
            }
            return {};
        }
        if (expr.op == "&") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            return got.empty() ? std::string{} : "*" + got;
        }
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported unary operator: " + expr.op);
        }
        return {};
    case ExprKind::Binary: {
        if (expr.children.size() != 2 || missing_expr(expr.children[0]) ||
            missing_expr(expr.children[1])) {
            if (use_location != nullptr) {
                sema_expr_fail(*use_location,
                               "operator " + expr.op + " expects left and right operands");
            }
            return {};
        }
        const std::string left = infer_expr_ast(scope, expr.children[0], use_location);
        const std::string right = infer_expr_ast(scope, expr.children[1], use_location);
        if (expr.op == "and" || expr.op == "or") {
            if (use_location != nullptr && !left.empty() && left != "bool") {
                sema_expr_fail(*use_location, expr.op + " expects bool, got " + left);
            }
            if (use_location != nullptr && !right.empty() && right != "bool") {
                sema_expr_fail(*use_location, expr.op + " expects bool, got " + right);
            }
            return "bool";
        }
        if (is_comparison_op(expr.op)) {
            if (contextual_numeric_binary_type(scope, expr.children[0], left, expr.children[1],
                                               right)) {
                return "bool";
            }
            if (const auto signature = binary_operator_signature(scope.symbols, expr.op, left,
                                                                 expr.children[1], right)) {
                if (use_location != nullptr) {
                    if (signature->params.size() != 1) {
                        sema_expr_fail(*use_location, "operator " + expr.op +
                                                          " expects 1 argument, got " +
                                                          std::to_string(signature->params.size()));
                    } else if (!can_assign_ast(scope, signature->params.front(), expr.children[1],
                                               right)) {
                        sema_expr_fail(*use_location, "operator " + expr.op + " expects " +
                                                          signature->params.front() + ", got " +
                                                          right);
                    }
                    if (!type_ref_is_name(signature_return_type_ref(*signature), "bool")) {
                        sema_expr_fail(*use_location,
                                       "comparison operator " + expr.op + " must return bool");
                    }
                }
                return "bool";
            }
            if (use_location != nullptr && !left.empty() && !right.empty() &&
                !comparison_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
                sema_expr_fail(*use_location,
                               "comparison " + expr.op + " expects " + left + ", got " + right);
            }
            return "bool";
        }
        if (is_arithmetic_op(expr.op)) {
            if (const auto contextual = contextual_numeric_binary_type(
                    scope, expr.children[0], left, expr.children[1], right)) {
                return *contextual;
            }
        }
        if (const auto signature =
                binary_operator_signature(scope.symbols, expr.op, left, expr.children[1], right)) {
            if (use_location != nullptr) {
                check_call_args_ast(scope, expr.op, *signature, std::vector<Expr>{expr.children[1]},
                                    use_location);
            }
            return signature_return_type_text(*signature);
        }
        if (use_location != nullptr && !left.empty() && !right.empty() &&
            !binary_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
            sema_expr_fail(*use_location,
                           "operator " + expr.op + " expects " + left + ", got " + right);
        }
        return left.empty() ? right : left;
    }
    case ExprKind::Member:
        if (const auto variant = enum_variant_from_expr(scope.symbols, expr)) {
            if (!variant->second->payload_fields.empty() && use_location != nullptr) {
                sema_expr_fail(*use_location, "payload enum variant requires construction: " +
                                                  variant->first->name + "." +
                                                  variant->second->name);
            }
            return variant->first->name;
        }
        if (const auto native = native_member_expr_type(scope.symbols, expr)) {
            return *native;
        }
        if (expr.children.size() == 1 && expr.children.front().kind == ExprKind::Name &&
            scope.locals.contains(expr.children.front().name)) {
            const Expr& receiver = expr.children.front();
            const std::string receiver_type = infer_expr_ast(scope, receiver, use_location);
            if (!receiver_type.empty() &&
                !field_type_for_type(scope.symbols, receiver_type, expr.name)) {
                if (const auto swizzle =
                        swizzle_type_for_type(scope.symbols, receiver_type, expr.name)) {
                    return *swizzle;
                }
            }
        }
        if (const std::string found = member_expr_type(scope.symbols, scope.locals, use_location,
                                                       expr, {}, scope.current_class);
            !found.empty()) {
            return found;
        }
        if (expr.children.size() == 1) {
            const Expr& receiver = expr.children.front();
            const std::string receiver_type = infer_expr_ast(scope, receiver, use_location);
            if (receiver_type.empty()) {
                return {};
            }
            if (const auto field = field_type_for_type(scope.symbols, receiver_type, expr.name)) {
                return *field;
            }
            if (const auto swizzle =
                    swizzle_type_for_type(scope.symbols, receiver_type, expr.name)) {
                return *swizzle;
            }
            if (foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type))) {
                return "auto";
            }
            if (use_location != nullptr) {
                sema_expr_fail(*use_location, "unknown field: " + receiver_type + "." + expr.name);
            }
            return {};
        }
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported member expression: " + display_expr(expr));
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
