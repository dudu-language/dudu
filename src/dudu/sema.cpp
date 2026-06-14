#include "dudu/sema.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/build_flags.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/escapes.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_constexpr.hpp"
#include "dudu/sema_constructors.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_expr.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_native.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/type_compat.hpp"
#include "dudu/unsupported.hpp"

#include <cctype>
#include <set>
#include <sstream>
namespace dudu {
namespace {
[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}
std::string infer_expr(const FunctionScope& scope, std::string expr,
                       const SourceLocation* location = nullptr);
std::string infer_expr_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* location = nullptr);
std::vector<std::string> call_args(std::string expr, size_t open) {
    std::string args = trim(expr.substr(open + 1, expr.size() - open - 2));
    return args.empty() ? std::vector<std::string>{} : split_top_level_args(args);
}
bool can_assign_expr(const FunctionScope& scope, const std::string& expected,
                     const std::string& expr, const std::string& got) {
    return assignment_type_allowed(expected, expr, got) ||
           assignment_type_allowed(resolve_alias(scope.symbols, expected), expr,
                                   resolve_alias(scope.symbols, got)) ||
           native_base_assignable(scope.symbols, expected, got);
}
bool is_builtin_call(const std::string& callee) {
    static const std::set<std::string> builtins = {"align_up", "delete", "free",  "len",
                                                   "max",      "min",    "print", "range"};
    return builtins.contains(callee);
}
bool is_local_member_call(const FunctionScope& scope, const std::string& callee) {
    const size_t dot = callee.find('.');
    return dot != std::string::npos && scope.locals.contains(trim(callee.substr(0, dot)));
}
bool freestanding_like(const FunctionScope& scope) {
    return scope.target_mode == "freestanding" || scope.target_mode == "embedded";
}

bool is_array_literal(const Expr& expr) {
    return expr.kind == ExprKind::ListLiteral;
}

bool is_comparison_op(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

const SourceLocation& node_location(const SourceLocation& fallback, const Expr& expr) {
    return expr.range.end.column > expr.range.start.column ? expr.location : fallback;
}

const SourceLocation& node_location(const SourceLocation& fallback, const TypeRef& type) {
    return type.range.end.column > type.range.start.column ? type.location : fallback;
}

bool foreign_cpp_type_name(const std::string& type) {
    return type.find('.') != std::string::npos || type.find("::") != std::string::npos;
}

void check_call_args(const FunctionScope& scope, const std::string& callee,
                     const FunctionSignature& signature, const std::vector<std::string>& args,
                     const SourceLocation* location) {
    if (location == nullptr)
        return;
    if ((!signature.variadic && args.size() != signature.params.size()) ||
        (signature.variadic && args.size() < signature.params.size())) {
        fail(*location, "function " + callee + " expects " +
                            std::to_string(signature.params.size()) + " arguments, got " +
                            std::to_string(args.size()));
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string got = infer_expr(scope, args[i], location);
        const std::string& expected = signature.params[i];
        if (!can_assign_expr(scope, expected, args[i], got)) {
            fail(*location, "argument " + std::to_string(i + 1) + " for " + callee + " expects " +
                                expected + ", got " + got);
        }
    }
}

void check_call_args_ast(const FunctionScope& scope, const std::string& callee,
                         const FunctionSignature& signature, const std::vector<Expr>& args,
                         const SourceLocation* location) {
    if (location == nullptr)
        return;
    if ((!signature.variadic && args.size() != signature.params.size()) ||
        (signature.variadic && args.size() < signature.params.size())) {
        fail(*location, "function " + callee + " expects " +
                            std::to_string(signature.params.size()) + " arguments, got " +
                            std::to_string(args.size()));
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string got = infer_expr_ast(scope, args[i], location);
        const std::string& expected = signature.params[i];
        if (!can_assign_expr(scope, expected, args[i].text, got)) {
            fail(*location, "argument " + std::to_string(i + 1) + " for " + callee + " expects " +
                                expected + ", got " + got);
        }
    }
}

bool call_args_match(const FunctionScope& scope, const FunctionSignature& signature,
                     const std::vector<std::string>& args) {
    if ((!signature.variadic && args.size() != signature.params.size()) ||
        (signature.variadic && args.size() < signature.params.size())) {
        return false;
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string got = infer_expr(scope, args[i], nullptr);
        if (!can_assign_expr(scope, signature.params[i], args[i], got)) {
            return false;
        }
    }
    return true;
}

bool call_args_match_ast(const FunctionScope& scope, const FunctionSignature& signature,
                         const std::vector<Expr>& args) {
    if ((!signature.variadic && args.size() != signature.params.size()) ||
        (signature.variadic && args.size() < signature.params.size())) {
        return false;
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string got = infer_expr_ast(scope, args[i], nullptr);
        if (!can_assign_expr(scope, signature.params[i], args[i].text, got)) {
            return false;
        }
    }
    return true;
}

std::optional<FunctionSignature> matching_signature(const FunctionScope& scope,
                                                    const std::vector<FunctionSignature>& options,
                                                    const std::vector<std::string>& args) {
    for (const FunctionSignature& signature : options) {
        if (call_args_match(scope, signature, args)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
matching_signature_ast(const FunctionScope& scope, const std::vector<FunctionSignature>& options,
                       const std::vector<Expr>& args) {
    for (const FunctionSignature& signature : options) {
        if (call_args_match_ast(scope, signature, args)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::string infer_expr(const FunctionScope& scope, std::string expr,
                       const SourceLocation* location) {
    expr = trim(std::move(expr));
    if (expr.empty())
        return "void";
    if (starts_with(expr, "{") && expr.back() == '}') {
        for (const std::string& entry : split_top_level(expr.substr(1, expr.size() - 2))) {
            if (find_top_level_char(entry, ':') != std::string::npos)
                return "dict";
        }
        return "set";
    }
    if (starts_with(expr, "lambda "))
        return "lambda";
    if (const auto type = infer_not_expr(scope, expr, location, infer_expr)) {
        return *type;
    }
    const size_t pointer_cast_call = find_call_open(expr);
    if (expr.size() > 1 && expr.front() == '*' && pointer_cast_call != std::string::npos &&
        find_call_close(expr, pointer_cast_call) == expr.size() - 1) {
        const std::string type = trim(expr.substr(1, pointer_cast_call - 1));
        if (known_type(scope.symbols, type)) {
            for (const std::string& arg : call_args(expr, pointer_cast_call)) {
                (void)infer_expr(scope, arg, location);
            }
            return "*" + type;
        }
    }
    if (expr.size() > 1 && expr.front() == '*') {
        const std::string name = trim(expr.substr(1));
        if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
            std::string type = trim(local->second);
            if (!type.empty() && type.front() == '*') {
                return trim(type.substr(1));
            }
        }
    }
    if (expr.size() > 1 && expr.front() == '&') {
        const std::string name = trim(expr.substr(1));
        if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
            return "*" + trim(local->second);
        }
        const std::string value_type = infer_expr(scope, name, location);
        if (!value_type.empty() && value_type != "void") {
            return "*" + value_type;
        }
    }
    const size_t call = find_call_open(expr);
    if (call != std::string::npos && find_call_close(expr, call) == expr.size() - 1) {
        const std::string callee = trim(expr.substr(0, call));
        if (const auto type =
                infer_allocation_call(scope.symbols, location, callee, call_args(expr, call)))
            return *type;
        if (is_deallocation_call(callee)) {
            std::vector<std::string> types;
            for (const std::string& arg : call_args(expr, call))
                types.push_back(infer_expr(scope, arg, location));
            if (location != nullptr)
                check_deallocation_args(*location, callee, types);
            return "void";
        }
        if (callee == "Ok" || callee == "Err") {
            const std::vector<std::string> args = call_args(expr, call);
            if (args.size() != 1 && location != nullptr) {
                fail(*location, callee + " expects 1 argument, got " + std::to_string(args.size()));
            }
            return callee + "[" +
                   (args.size() == 1 ? infer_expr(scope, args.front(), location) : "") + "]";
        }
        if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee));
            klass != scope.symbols.classes.end()) {
            check_constructor_args(
                scope, *klass->second, call_args(expr, call), location, infer_expr,
                [&](const std::string& expected, const std::string& value, const std::string& got) {
                    return can_assign_expr(scope, expected, value, got);
                });
            return callee;
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args(scope, callee, fn->second, call_args(expr, call), location);
            return fn->second.return_type;
        }
        if (const auto signature = native_signature_for_call(
                scope, callee, call_args(expr, call), location, infer_expr,
                [&](const std::string& expected, const std::string& value, const std::string& got) {
                    return can_assign_expr(scope, expected, value, got);
                })) {
            return signature->return_type;
        }
        if (!is_local_member_call(scope, callee) && callee.find('.') == std::string::npos &&
            known_type(scope.symbols, callee)) {
            return callee;
        }
        if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
            FunctionSignature signature;
            if (parse_function_type(resolve_alias(scope.symbols, local->second), signature)) {
                check_call_args(scope, callee, signature, call_args(expr, call), location);
                return signature.return_type;
            }
        }
        const size_t method_dot = callee.rfind('.');
        if (method_dot != std::string::npos && is_member_path(callee)) {
            const std::string receiver = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            FunctionSignature signature;
            if (!scope.locals.contains(receiver) && scope.symbols.classes.contains(receiver) &&
                static_method_signature_for_type(scope.symbols, receiver, method_name, signature,
                                                 location)) {
                check_call_args(scope, callee, signature, call_args(expr, call), location);
                return signature.return_type;
            }
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, nullptr, receiver, "");
            if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                          location)) {
                const std::vector<std::string> args = call_args(expr, call);
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type, method_name);
                if (const auto match = matching_signature(scope, signatures, args)) {
                    check_call_args(scope, callee, *match, args, location);
                    return match->return_type;
                }
                if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                    for (const std::string& arg : args) {
                        (void)infer_expr(scope, arg, location);
                    }
                    return "auto";
                }
                check_call_args(scope, callee, signature, args, location);
                return signature.return_type;
            }
        }
        if (location != nullptr && callee.find('.') == std::string::npos &&
            callee.find('[') == std::string::npos && is_plain_identifier(callee) &&
            !known_type(scope.symbols, callee) && !is_builtin_call(callee)) {
            if (is_dudu_all_caps(callee))
                return "auto";
            fail(*location, "unknown function: " + callee);
        }
    }
    const std::vector<std::string> tuple_parts = split_top_level(expr);
    if (tuple_parts.size() > 1) {
        std::ostringstream out;
        out << "tuple[";
        for (size_t i = 0; i < tuple_parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << infer_expr(scope, tuple_parts[i], location);
        }
        out << "]";
        return out.str();
    }
    if (expr == "True" || expr == "False") {
        return "bool";
    }
    if (is_string_literal_expr(expr)) {
        return "str";
    }
    if (const auto type = infer_logical_expr(scope, expr, location, infer_expr)) {
        return *type;
    }
    if (const auto type = infer_comparison_expr(scope, expr, location, infer_expr)) {
        return *type;
    }
    const size_t op = find_top_level_operator(expr);
    if (op != std::string::npos) {
        const std::string left = infer_expr(scope, expr.substr(0, op), location);
        const std::string op_text = top_level_operator_text(expr, op);
        const std::string right_expr = expr.substr(op + op_text.size());
        const std::string right = infer_expr(scope, right_expr, location);
        if (const auto signature = dudu_operator_signature(scope.symbols, op_text, left)) {
            if (location != nullptr) {
                check_call_args(scope, op_text, *signature, {right_expr}, location);
            }
            return signature->return_type;
        }
        if (location != nullptr && !left.empty() && !right.empty() &&
            !binary_rhs_allowed(scope.symbols, op_text, left, right_expr, right)) {
            fail(*location, "operator " + op_text + " expects " + left + ", got " + right);
        }
        return left.empty() ? right : left;
    }
    if (std::isdigit(static_cast<unsigned char>(expr.front())) != 0) {
        return expr.find('.') == std::string::npos ? "i32" : "f64";
    }
    if (expr == "None") {
        return "None";
    }
    const size_t index = expr.find('[');
    if (location != nullptr && index != std::string::npos && expr.back() == ']') {
        const std::string name = trim(expr.substr(0, index));
        const std::string index_expr = expr.substr(index + 1, expr.size() - index - 2);
        if (is_plain_identifier(name)) {
            if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
                if (const auto signature =
                        dudu_operator_signature(scope.symbols, "[]", local->second)) {
                    check_call_args(scope, name + "[]", *signature,
                                    split_top_level_args(index_expr), location);
                }
            }
            return indexed_value_type(scope.symbols, scope.locals, *location, name, index_expr,
                                      "indexed access to unknown local: ");
        }
        if (is_member_path(name)) {
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, location, name, "");
            if (!receiver_type.empty()) {
                return indexed_type_from_type(scope.symbols, *location, receiver_type, index_expr,
                                              name);
            }
        }
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos && is_member_path(expr)) {
        return member_path_type(scope.symbols, scope.locals, location, expr, "");
    }
    if (const auto local = scope.locals.find(expr); local != scope.locals.end()) {
        return local->second;
    }
    if (const auto fn = scope.symbols.function_signatures.find(expr);
        fn != scope.symbols.function_signatures.end()) {
        return function_type(fn->second);
    }
    if (const auto value = scope.symbols.native_values.find(expr);
        value != scope.symbols.native_values.end()) {
        return value->second;
    }
    if (const auto native = scope.symbols.native_function_signatures.find(expr);
        native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
        return function_type(native->second.front());
    }
    if (is_dudu_all_caps(expr))
        return "i32";
    if (location != nullptr && is_plain_identifier(expr)) {
        fail(*location, "unknown identifier: " + expr);
    }
    return {};
}

std::string infer_expr_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* location) {
    const SourceLocation* use_location =
        location != nullptr
            ? location
            : (expr.range.end.column > expr.range.start.column ? &expr.location : nullptr);
    switch (expr.kind) {
    case ExprKind::Unknown:
        return infer_expr(scope, expr.text, use_location);
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
        return "lambda";
    case ExprKind::ListLiteral:
        return "list";
    case ExprKind::DictLiteral:
        return "dict";
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
            fail(*use_location, "unknown identifier: " + expr.name);
        }
        return {};
    case ExprKind::Unary:
        if (expr.children.empty()) {
            return infer_expr(scope, expr.text, use_location);
        }
        if (expr.op == "not") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            if (use_location != nullptr && !got.empty() && got != "bool") {
                fail(*use_location, "not expects bool, got " + got);
            }
            return "bool";
        }
        if (expr.op == "-") {
            return infer_expr_ast(scope, expr.children.front(), use_location);
        }
        return infer_expr(scope, expr.text, use_location);
    case ExprKind::Binary: {
        if (expr.children.size() != 2) {
            return infer_expr(scope, expr.text, use_location);
        }
        const std::string left = infer_expr_ast(scope, expr.children[0], use_location);
        const std::string right = infer_expr_ast(scope, expr.children[1], use_location);
        if (expr.op == "and" || expr.op == "or") {
            if (use_location != nullptr && !left.empty() && left != "bool") {
                fail(*use_location, expr.op + " expects bool, got " + left);
            }
            if (use_location != nullptr && !right.empty() && right != "bool") {
                fail(*use_location, expr.op + " expects bool, got " + right);
            }
            return "bool";
        }
        if (is_comparison_op(expr.op)) {
            if (const auto signature = dudu_operator_signature(scope.symbols, expr.op, left)) {
                if (use_location != nullptr) {
                    if (signature->params.size() != 1) {
                        fail(*use_location, "operator " + expr.op + " expects 1 argument, got " +
                                                std::to_string(signature->params.size()));
                    } else if (!can_assign_expr(scope, signature->params.front(),
                                                expr.children[1].text, right)) {
                        fail(*use_location, "operator " + expr.op + " expects " +
                                                signature->params.front() + ", got " + right);
                    }
                    if (signature->return_type != "bool") {
                        fail(*use_location, "comparison operator " + expr.op + " must return bool");
                    }
                }
                return "bool";
            }
            if (use_location != nullptr && !left.empty() && !right.empty() &&
                !comparison_rhs_allowed(scope.symbols, expr.op, left, expr.children[1].text,
                                        right)) {
                fail(*use_location,
                     "comparison " + expr.op + " expects " + left + ", got " + right);
            }
            return "bool";
        }
        if (const auto signature = dudu_operator_signature(scope.symbols, expr.op, left)) {
            if (use_location != nullptr) {
                check_call_args_ast(scope, expr.op, *signature, std::vector<Expr>{expr.children[1]},
                                    use_location);
            }
            return signature->return_type;
        }
        if (use_location != nullptr && !left.empty() && !right.empty() &&
            !binary_rhs_allowed(scope.symbols, expr.op, left, expr.children[1].text, right)) {
            fail(*use_location, "operator " + expr.op + " expects " + left + ", got " + right);
        }
        return left.empty() ? right : left;
    }
    case ExprKind::Member:
        return member_path_type(scope.symbols, scope.locals, use_location, expr.text, "");
    case ExprKind::Index:
        if (use_location != nullptr && expr.children.size() == 2) {
            const Expr& receiver = expr.children[0];
            const std::string& index_expr = expr.children[1].text;
            if (receiver.kind == ExprKind::Name) {
                if (const auto local = scope.locals.find(receiver.name);
                    local != scope.locals.end()) {
                    if (const auto signature =
                            dudu_operator_signature(scope.symbols, "[]", local->second)) {
                        check_call_args(scope, receiver.name + "[]", *signature,
                                        split_top_level_args(index_expr), use_location);
                    }
                }
                return indexed_value_type(scope.symbols, scope.locals, *use_location, receiver.name,
                                          index_expr, "indexed access to unknown local: ");
            }
            if (is_member_path(receiver.text)) {
                const std::string receiver_type =
                    member_path_type(scope.symbols, scope.locals, use_location, receiver.text, "");
                if (!receiver_type.empty()) {
                    return indexed_type_from_type(scope.symbols, *use_location, receiver_type,
                                                  index_expr, receiver.text);
                }
            }
        }
        return infer_expr(scope, expr.text, use_location);
    case ExprKind::Conditional:
        if (expr.children.size() == 3) {
            const std::string condition = infer_expr_ast(scope, expr.children[1], use_location);
            if (use_location != nullptr && !condition.empty() && condition != "bool") {
                fail(*use_location, "condition must be bool, got " + condition);
            }
            const std::string then_type = infer_expr_ast(scope, expr.children[0], use_location);
            const std::string else_type = infer_expr_ast(scope, expr.children[2], use_location);
            return then_type.empty() ? else_type : then_type;
        }
        return infer_expr(scope, expr.text, use_location);
    case ExprKind::Call: {
        const std::string callee = trim(expr.name);
        if (known_type(scope.symbols, callee) ||
            scope.symbols.classes.contains(resolve_alias(scope.symbols, callee))) {
            return infer_expr(scope, expr.text, use_location);
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args_ast(scope, callee, fn->second, expr.children, use_location);
            return fn->second.return_type;
        }
        if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
            FunctionSignature signature;
            if (parse_function_type(resolve_alias(scope.symbols, local->second), signature)) {
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
        }
        const size_t method_dot = callee.rfind('.');
        if (method_dot != std::string::npos && is_member_path(callee)) {
            const std::string receiver = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            FunctionSignature signature;
            if (!scope.locals.contains(receiver) && scope.symbols.classes.contains(receiver) &&
                static_method_signature_for_type(scope.symbols, receiver, method_name, signature,
                                                 use_location)) {
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, nullptr, receiver, "");
            if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                          use_location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type, method_name);
                if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
                    check_call_args_ast(scope, callee, *match, expr.children, use_location);
                    return match->return_type;
                }
                if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                    for (const Expr& arg : expr.children) {
                        (void)infer_expr_ast(scope, arg, use_location);
                    }
                    return "auto";
                }
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
        }
        return infer_expr(scope, expr.text, use_location);
    }
    case ExprKind::TemplateCall:
    case ExprKind::CppEscape:
        return infer_expr(scope, expr.text, use_location);
    }
    return infer_expr(scope, expr.text, use_location);
}

void check_type_match(const FunctionScope& scope, const std::string& expected, const Expr& expr,
                      const SourceLocation& location) {
    const std::string got = infer_expr_ast(scope, expr, &location);
    if (can_assign_expr(scope, expected, expr.text, got)) {
        return;
    }
    fail(location, assignment_error(expected, expr.text, got));
}

void check_array_literal_elements(const FunctionScope& scope, const std::string& element_type,
                                  const Expr& expr, const SourceLocation& location) {
    if (expr.kind == ExprKind::ListLiteral) {
        for (const Expr& child : expr.children) {
            check_array_literal_elements(scope, element_type, child, location);
        }
        return;
    }
    const std::string got = infer_expr_ast(scope, expr, &expr.location);
    if (!can_assign_expr(scope, element_type, expr.text, got)) {
        fail(location, "array literal element expects " + element_type + ", got " + got);
    }
}

std::string effective_var_type(const Stmt& stmt) {
    const ArrayShapeInference inferred = infer_array_literal_shape_type(stmt.type, stmt.value_expr);
    return inferred.status == ArrayShapeStatus::Inferred ? inferred.type : stmt.type;
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

void check_condition_type(const FunctionScope& scope, const Stmt& stmt, std::string expr) {
    expr = trim(std::move(expr));
    if (!expr.empty() && expr.back() == ':') {
        expr.pop_back();
    }
    const SourceLocation& location = node_location(stmt.location, stmt.condition_expr);
    const Expr condition_expr =
        trim(stmt.condition) == expr ? stmt.condition_expr : parse_expr_text(expr, location);
    const std::string got = infer_expr_ast(scope, condition_expr, &location);
    if (!got.empty() && got != "bool" && got != "auto") {
        if (const auto signature = dudu_operator_signature(scope.symbols, "bool", got);
            signature && signature->params.empty() && signature->return_type == "bool") {
            return;
        }
        fail(location, "condition must be bool, got " + got);
    }
}
std::string assign_target_type(const FunctionScope& scope, const Stmt& stmt,
                               const std::string& lhs) {
    const SourceLocation& target_location = node_location(stmt.location, stmt.target_expr);
    if (lhs.size() > 1 && lhs.front() == '*') {
        const std::string name = trim(lhs.substr(1));
        const auto local = scope.locals.find(name);
        if (local == scope.locals.end()) {
            fail(target_location, "assignment through unknown local: " + name);
        }
        std::string type = trim(local->second);
        if (type.empty() || type.front() != '*') {
            fail(target_location, "cannot dereference non-pointer: " + name);
        }
        return trim(type.substr(1));
    }
    if (lhs.find('.') == std::string::npos) {
        const size_t index = lhs.find('[');
        if (index != std::string::npos) {
            const std::string name = trim(lhs.substr(0, index));
            const std::string index_expr = lhs.substr(index + 1, lhs.size() - index - 2);
            return indexed_value_type(scope.symbols, scope.locals, target_location, name,
                                      index_expr, "indexed assignment to unknown local: ");
        }
        if (scope.constants.contains(lhs)) {
            fail(target_location, "cannot assign to constant: " + lhs);
        }
        const auto local = scope.locals.find(lhs);
        if (local == scope.locals.end()) {
            fail(target_location, "assignment to unknown local: " + lhs);
        }
        return local->second;
    }
    return member_path_type(scope.symbols, scope.locals, &target_location, lhs,
                            "assignment through unknown local: ");
}
void check_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                int loop_depth);
void check_block(FunctionScope& scope, const std::vector<Stmt>& body,
                 const std::string& return_type, int loop_depth) {
    for (const Stmt& stmt : body) {
        check_stmt(scope, stmt, return_type, loop_depth);
    }
}
void check_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                int loop_depth) {
    const std::string text = trim(stmt.text);
    check_local_address_escape(stmt, scope.locals);
    if (stmt.kind == StmtKind::Return) {
        const std::string expr = stmt.value;
        const SourceLocation& value_location = node_location(stmt.location, stmt.value_expr);
        const std::string got = infer_expr_ast(scope, stmt.value_expr, &value_location);
        if (return_type == "void" && got != "void")
            fail(value_location, "void function cannot return " + got);
        if (return_type != "void" && !can_assign_expr(scope, return_type, expr, got)) {
            fail(value_location, "return type mismatch: expected " + return_type + ", got " + got);
        }
        return;
    }
    if (stmt.kind == StmtKind::Assert || stmt.kind == StmtKind::DebugAssert) {
        const bool debug = stmt.kind == StmtKind::DebugAssert;
        if (!debug && freestanding_like(scope)) {
            fail(stmt.location,
                 "runtime assert is not available in " + scope.target_mode +
                     " target mode; use debug_assert or a target-specific assert handler");
        }
        const auto parts = split_top_level_args(stmt.condition);
        check_condition_type(scope, stmt, parts.empty() ? "" : parts.front());
        if (parts.size() > 1)
            (void)infer_expr(scope, parts[1], &node_location(stmt.location, stmt.condition_expr));
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        const std::string expr = stmt.value;
        if (!expr.empty())
            (void)infer_expr_ast(scope, stmt.value_expr,
                                 &node_location(stmt.location, stmt.value_expr));
        return;
    }
    if (stmt.kind == StmtKind::CppEscape || stmt.kind == StmtKind::Pass)
        return;
    if (starts_with(text, "delete ")) {
        check_deallocation_args(stmt.location, "delete",
                                {infer_expr(scope, text.substr(7), &stmt.location)});
        return;
    }
    if ((stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) && loop_depth == 0) {
        fail(stmt.location, text + " outside loop");
    }
    if (stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) {
        return;
    }
    if (stmt.kind == StmtKind::If) {
        check_condition_type(scope, stmt, stmt.condition);
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        check_condition_type(scope, stmt, stmt.condition);
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Else) {
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Try) {
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        FunctionScope nested = scope;
        std::string header = trim(text.substr(6));
        if (!header.empty() && header.back() == ':')
            header.pop_back();
        const size_t colon = find_top_level_char(header, ':');
        if (!header.empty() && colon == std::string::npos)
            fail(stmt.location, "expected except binding as name: Type");
        if (colon != std::string::npos) {
            const std::string name = trim(header.substr(0, colon));
            const std::string type = trim(header.substr(colon + 1));
            check_local_binding_name(stmt.location, name);
            if (!known_type(scope.symbols, type))
                fail(stmt.location, "unknown catch type: " + type);
            nested.locals[name] = "&const[" + type + "]";
        }
        check_block(nested, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::While) {
        check_condition_type(scope, stmt, stmt.condition);
        check_block(scope, stmt.children, return_type, loop_depth + 1);
        return;
    }
    if (stmt.kind == StmtKind::For) {
        FunctionScope nested = scope;
        if (!stmt.name.empty() && !stmt.type.empty() && !stmt.iterable.empty()) {
            check_local_binding_name(stmt.location, stmt.name);
            check_iterable_binding(scope.symbols, scope.locals,
                                   node_location(stmt.location, stmt.iterable_expr), stmt.type,
                                   stmt.iterable);
            nested.locals[stmt.name] = stmt.type;
        }
        check_block(nested, stmt.children, return_type, loop_depth + 1);
        return;
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        const std::string target_type = assign_target_type(scope, stmt, stmt.target);
        if (!target_type.empty()) {
            check_type_match(scope, target_type, stmt.value_expr,
                             node_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        check_local_binding_name(stmt.location, stmt.name);
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(stmt.type, stmt.value_expr);
        if (inferred.status == ArrayShapeStatus::EmptyLiteral) {
            fail(node_location(stmt.location, stmt.value_expr),
                 "array shape cannot be inferred from an empty literal");
        }
        if (inferred.status == ArrayShapeStatus::RaggedLiteral) {
            fail(node_location(stmt.location, stmt.value_expr), "ragged array literal");
        }
        const std::vector<size_t> explicit_shape = explicit_array_shape(stmt.type);
        const std::string explicit_element = explicit_array_element_type(stmt.type);
        if (!explicit_shape.empty() && stmt.value_expr.kind == ExprKind::ListLiteral) {
            const ArrayShapeInference actual =
                infer_array_literal_shape_type("array[" + explicit_element + "]", stmt.value_expr);
            if (actual.status == ArrayShapeStatus::RaggedLiteral) {
                fail(node_location(stmt.location, stmt.value_expr), "ragged array literal");
            }
            if (actual.status == ArrayShapeStatus::EmptyLiteral &&
                explicit_shape != std::vector<size_t>{0}) {
                fail(node_location(stmt.location, stmt.value_expr),
                     "array literal shape mismatch: expected " + shape_text(explicit_shape) +
                         ", got [0]");
            }
            if (actual.status == ArrayShapeStatus::Inferred && actual.shape != explicit_shape) {
                fail(node_location(stmt.location, stmt.value_expr),
                     "array literal shape mismatch: expected " + shape_text(explicit_shape) +
                         ", got " + shape_text(actual.shape));
            }
        }
        const std::string type = effective_var_type(stmt);
        if (!known_type(scope.symbols, type)) {
            fail(node_location(stmt.location, stmt.type_ref), "unknown local type: " + stmt.type);
        }
        if (!stmt.value.empty()) {
            if (inferred.status == ArrayShapeStatus::Inferred &&
                is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, inferred.element_type, stmt.value_expr,
                                             node_location(stmt.location, stmt.value_expr));
            } else if (!explicit_element.empty() && is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, explicit_element, stmt.value_expr,
                                             node_location(stmt.location, stmt.value_expr));
            } else {
                check_type_match(scope, type, stmt.value_expr,
                                 node_location(stmt.location, stmt.value_expr));
            }
        }
        scope.locals[stmt.name] = type;
        if (is_dudu_all_caps(stmt.name)) {
            scope.constants.insert(stmt.name);
        }
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        const std::string lhs = stmt.target;
        if (split_top_level(lhs).size() > 1) {
            const std::vector<std::string> names = split_top_level(lhs);
            const std::vector<std::string> types = tuple_types(
                scope.symbols, infer_expr_ast(scope, stmt.value_expr,
                                              &node_location(stmt.location, stmt.value_expr)));
            if (names.size() != types.size()) {
                fail(node_location(stmt.location, stmt.value_expr),
                     "tuple destructuring count mismatch");
            }
            check_destructure_bindings(stmt.location, names, scope.locals);
            for (size_t i = 0; i < names.size(); ++i) {
                scope.locals[names[i]] = types[i];
            }
            return;
        }
        if (lhs.find('.') == std::string::npos && !starts_with(lhs, "*") &&
            lhs.find('[') == std::string::npos && !scope.locals.contains(lhs)) {
            check_local_binding_name(stmt.location, lhs);
            const std::string inferred = infer_expr_ast(
                scope, stmt.value_expr, &node_location(stmt.location, stmt.value_expr));
            scope.locals[lhs] = inferred.empty() ? "auto" : inferred;
            if (is_dudu_all_caps(lhs)) {
                scope.constants.insert(lhs);
            }
            return;
        }
        const std::string target_type = assign_target_type(scope, stmt, lhs);
        if (!target_type.empty()) {
            check_type_match(scope, target_type, stmt.value_expr,
                             node_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    (void)infer_expr_ast(scope, stmt.expr, &stmt.location);
}
void check_bodies(const ModuleAst& module, const Symbols& symbols) {
    FunctionScope base{symbols, {}, {}};
    const auto mode = module.build_values.find("TARGET_MODE");
    if (mode != module.build_values.end()) {
        base.target_mode = trim(mode->second);
        if (base.target_mode.size() >= 2 && base.target_mode.front() == '"' &&
            base.target_mode.back() == '"') {
            base.target_mode = base.target_mode.substr(1, base.target_mode.size() - 2);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        base.locals[constant.name] = constant.type;
        base.constants.insert(constant.name);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            FunctionScope scope = base;
            for (const ParamDecl& param : method.params) {
                scope.locals[param.name] = param.type;
            }
            check_block(scope, method.statements,
                        method.return_type.empty() ? "void" : method.return_type, 0);
            if (!method.return_type.empty() && method.return_type != "void" &&
                !block_guarantees_return(method.statements)) {
                fail(method.location, "missing return in function: " + method.name);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        FunctionScope scope = base;
        for (const ParamDecl& param : fn.params) {
            scope.locals[param.name] = param.type;
        }
        check_block(scope, fn.statements, fn.return_type.empty() ? "void" : fn.return_type, 0);
        if (!fn.return_type.empty() && fn.return_type != "void" &&
            !block_guarantees_return(fn.statements)) {
            fail(fn.location, "missing return in function: " + fn.name);
        }
    }
}
} // namespace
void analyze_module(const ModuleAst& module, SemanticOptions options) {
    const Symbols symbols = collect_symbols(module);
    check_build_flags(module);
    check_naming(module);
    check_unsupported_python(module);
    check_declarations(module, symbols);
    check_constexpr_uses(module);
    if (options.check_bodies)
        check_bodies(module, symbols);
}
} // namespace dudu
