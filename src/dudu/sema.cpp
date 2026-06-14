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
#include <optional>
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

bool can_assign_ast(const FunctionScope& scope, const std::string& expected, const Expr& expr,
                    const std::string& got) {
    return assignment_type_allowed(expected, expr, got) ||
           assignment_type_allowed(resolve_alias(scope.symbols, expected), expr,
                                   resolve_alias(scope.symbols, got)) ||
           native_base_assignable(scope.symbols, expected, got);
}

std::string assignment_error_ast(const std::string& expected, const Expr& expr,
                                 const std::string& got) {
    return assignment_error(expected, expr, got);
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

bool has_expr(const Expr& expr) {
    return !expr.text.empty();
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

std::optional<std::string> member_path_from_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        return expr.name;
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 && !expr.name.empty()) {
        const std::optional<std::string> receiver = member_path_from_expr(expr.children.front());
        if (receiver.has_value()) {
            return *receiver + "." + expr.name;
        }
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
        const std::optional<std::string> receiver = member_path_from_expr(expr.children.front());
        if (receiver.has_value() && !expr.children[1].text.empty()) {
            return *receiver + "[" + expr.children[1].text + "]";
        }
    }
    return std::nullopt;
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
        if (!can_assign_ast(scope, expected, args[i], got)) {
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
        if (!can_assign_ast(scope, signature.params[i], args[i], got)) {
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

std::string template_call_callee(const Expr& expr) {
    std::ostringstream out;
    out << expr.name << "[";
    for (size_t i = 0; i < expr.template_args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << expr.template_args[i].text;
    }
    out << "]";
    return out.str();
}

std::string template_method_name(const Expr& expr, size_t method_dot) {
    std::ostringstream out;
    out << trim(expr.name.substr(method_dot + 1)) << "[";
    for (size_t i = 0; i < expr.template_args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << expr.template_args[i].text;
    }
    out << "]";
    return out.str();
}

bool known_template_constructor_type(const FunctionScope& scope, const std::string& callee) {
    const std::string base = base_type(callee);
    if (base.find('.') != std::string::npos || base.find("::") != std::string::npos) {
        return scope.symbols.types.contains(base) || scope.symbols.native_classes.contains(base) ||
               scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
    }
    return known_type(scope.symbols, callee) ||
           scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
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

std::string infer_template_call_ast(const FunctionScope& scope, const Expr& expr,
                                    const SourceLocation* location) {
    if (expr.template_args.empty()) {
        return infer_expr(scope, expr.text, location);
    }
    const std::string callee = template_call_callee(expr);

    if (const auto type = infer_allocation_call(scope.symbols, location, callee, expr.children)) {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return *type;
    }
    if (expr.name == "sizeof" || expr.name == "alignof") {
        if (location != nullptr && expr.template_args.size() != 1) {
            fail(*location, expr.name + " expects 1 type argument, got " +
                                std::to_string(expr.template_args.size()));
        }
        if (expr.template_args.size() == 1 && location != nullptr &&
            !known_type(scope.symbols, expr.template_args.front().text)) {
            fail(*location, "unknown " + expr.name + " type: " + expr.template_args.front().text);
        }
        return "usize";
    }
    if (expr.name == "offsetof") {
        if (location != nullptr && expr.template_args.size() != 1) {
            fail(*location, "offsetof expects 1 type argument, got " +
                                std::to_string(expr.template_args.size()));
        }
        if (location != nullptr && expr.children.size() != 1) {
            fail(*location,
                 "offsetof expects 1 field argument, got " + std::to_string(expr.children.size()));
        }
        if (expr.template_args.size() == 1 && location != nullptr &&
            !known_type(scope.symbols, expr.template_args.front().text)) {
            fail(*location, "unknown offsetof type: " + expr.template_args.front().text);
        }
        return "usize";
    }

    if (const auto signature = native_signature_for_call(
            scope, callee, expr.children, location, infer_expr_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            })) {
        return signature->return_type;
    }
    const size_t method_dot = expr.name.rfind('.');
    if (method_dot != std::string::npos && is_member_path(expr.name)) {
        const std::string receiver = trim(expr.name.substr(0, method_dot));
        const std::string method_name = template_method_name(expr, method_dot);
        FunctionSignature signature;
        if (!scope.locals.contains(receiver) && scope.symbols.classes.contains(receiver) &&
            static_method_signature_for_type(scope.symbols, receiver, method_name, signature,
                                             location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature.return_type;
        }
        const std::string receiver_type =
            member_path_type(scope.symbols, scope.locals, nullptr, receiver, "");
        if (scope.locals.contains(receiver) && (receiver_type.empty() || receiver_type == "auto")) {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_ast(scope, arg, location);
            }
            return "auto";
        }
        if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                      location)) {
            const std::vector<FunctionSignature> signatures =
                method_signatures_for_type(scope.symbols, receiver_type, method_name);
            if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
                check_call_args_ast(scope, callee, *match, expr.children, location);
                return match->return_type;
            }
            if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                for (const Expr& arg : expr.children) {
                    (void)infer_expr_ast(scope, arg, location);
                }
                return "auto";
            }
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature.return_type;
        }
        if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_ast(scope, arg, location);
            }
            return "auto";
        }
    }
    if (known_template_constructor_type(scope, callee)) {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return callee;
    }
    if (location != nullptr && expr.name.find('.') == std::string::npos &&
        is_plain_identifier(expr.name) && !known_type(scope.symbols, expr.name)) {
        fail(*location, "unknown function: " + callee);
    }
    return infer_expr(scope, expr.text, location);
}

std::string infer_constructor_call_ast(const FunctionScope& scope, const Expr& expr,
                                       const std::string& callee, const SourceLocation* location) {
    if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee));
        klass != scope.symbols.classes.end()) {
        check_constructor_args_ast(
            scope, *klass->second, expr.children, location, infer_expr_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            });
        return callee;
    }
    for (const Expr& arg : expr.children) {
        (void)infer_expr_ast(scope, arg, location);
    }
    return callee;
}

std::string infer_builtin_call_ast(const FunctionScope& scope, const Expr& expr,
                                   const std::string& callee, const SourceLocation* location) {
    auto check_arity = [&](size_t min, size_t max) {
        if (location != nullptr && (expr.children.size() < min || expr.children.size() > max)) {
            std::ostringstream message;
            message << callee << " expects ";
            if (min == max) {
                message << min;
            } else {
                message << min << " to " << max;
            }
            message << " arguments, got " << expr.children.size();
            fail(*location, message.str());
        }
    };

    if (callee == "len") {
        check_arity(1, 1);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "usize";
    }
    if (callee == "range") {
        check_arity(1, 3);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "range";
    }
    if (callee == "min" || callee == "max") {
        check_arity(2, 2);
        std::string first;
        for (size_t i = 0; i < expr.children.size(); ++i) {
            const std::string got = infer_expr_ast(scope, expr.children[i], location);
            if (i == 0) {
                first = got;
                continue;
            }
            if (location != nullptr && !can_assign_ast(scope, first, expr.children[i], got)) {
                fail(*location, callee + " argument 2 expects " + first + ", got " + got);
            }
        }
        return first.empty() ? "auto" : first;
    }
    if (callee == "align_up") {
        check_arity(2, 2);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "usize";
    }
    if (callee == "print") {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "void";
    }
    if (is_deallocation_call(callee)) {
        std::vector<std::string> types;
        for (const Expr& arg : expr.children) {
            types.push_back(infer_expr_ast(scope, arg, location));
        }
        if (location != nullptr) {
            check_deallocation_args(*location, callee, types);
        }
        return "void";
    }
    return {};
}

std::optional<std::string> infer_pointer_cast_call_ast(const FunctionScope& scope, const Expr& expr,
                                                       const std::string& callee,
                                                       const SourceLocation* location) {
    if (!starts_with(callee, "*")) {
        return std::nullopt;
    }
    const std::string type = trim(callee.substr(1));
    if (!known_type(scope.symbols, type)) {
        return std::nullopt;
    }
    for (const Expr& arg : expr.children) {
        (void)infer_expr_ast(scope, arg, location);
    }
    return "*" + type;
}

std::vector<Expr> index_arg_exprs(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children;
    }
    return {index_expr};
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
    case ExprKind::DictEntry:
        return "auto";
    case ExprKind::NamedArg:
        if (expr.children.size() == 1) {
            return infer_expr_ast(scope, expr.children.front(), use_location);
        }
        return "auto";
    case ExprKind::Slice:
        for (const Expr& child : expr.children) {
            if (!child.text.empty()) {
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
        if (expr.op == "*") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            const std::string type = trim(got);
            if (!type.empty() && type.front() == '*') {
                return trim(type.substr(1));
            }
            if (use_location != nullptr && !type.empty() && type != "auto") {
                fail(*use_location, "cannot dereference non-pointer: " + type);
            }
            return {};
        }
        if (expr.op == "&") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            return got.empty() ? std::string{} : "*" + got;
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
                    } else if (!can_assign_ast(scope, signature->params.front(), expr.children[1],
                                               right)) {
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
                !comparison_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
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
            !binary_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
            fail(*use_location, "operator " + expr.op + " expects " + left + ", got " + right);
        }
        return left.empty() ? right : left;
    }
    case ExprKind::Member:
        if (const std::optional<std::string> path = member_path_from_expr(expr)) {
            return member_path_type(scope.symbols, scope.locals, use_location, *path, "");
        }
        return infer_expr(scope, expr.text, use_location);
    case ExprKind::Index:
        if (use_location != nullptr && expr.children.size() == 2) {
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
                return indexed_value_type(scope.symbols, scope.locals, *use_location, receiver.name,
                                          expr.children[1], "indexed access to unknown local: ");
            }
            if (const std::optional<std::string> receiver_path = member_path_from_expr(receiver)) {
                const std::string receiver_type =
                    member_path_type(scope.symbols, scope.locals, use_location, *receiver_path, "");
                if (!receiver_type.empty()) {
                    return indexed_type_from_type(scope.symbols, *use_location, receiver_type,
                                                  expr.children[1], *receiver_path);
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
        if (const auto pointer_cast =
                infer_pointer_cast_call_ast(scope, expr, callee, use_location)) {
            return *pointer_cast;
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args_ast(scope, callee, fn->second, expr.children, use_location);
            return fn->second.return_type;
        }
        if (const auto signature = native_signature_for_call(
                scope, callee, expr.children, use_location, infer_expr_ast,
                [&](const std::string& expected, const Expr& value, const std::string& got) {
                    return can_assign_ast(scope, expected, value, got);
                })) {
            return signature->return_type;
        }
        if (known_template_constructor_type(scope, callee)) {
            return infer_constructor_call_ast(scope, expr, callee, use_location);
        }
        if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
            FunctionSignature signature;
            if (parse_function_type(resolve_alias(scope.symbols, local->second), signature)) {
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
        }
        if (is_builtin_call(callee)) {
            return infer_builtin_call_ast(scope, expr, callee, use_location);
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
        return infer_template_call_ast(scope, expr, use_location);
    case ExprKind::CppEscape:
        return infer_expr(scope, expr.text, use_location);
    }
    return infer_expr(scope, expr.text, use_location);
}

void check_type_match(const FunctionScope& scope, const std::string& expected, const Expr& expr,
                      const SourceLocation& location) {
    const std::string got = infer_expr_ast(scope, expr, &location);
    if (can_assign_ast(scope, expected, expr, got)) {
        return;
    }
    fail(location, assignment_error_ast(expected, expr, got));
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
    if (!can_assign_ast(scope, element_type, expr, got)) {
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

void check_condition_type(const FunctionScope& scope, const Stmt& stmt) {
    const SourceLocation& location = node_location(stmt.location, stmt.condition_expr);
    const std::string got = infer_expr_ast(scope, stmt.condition_expr, &location);
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
    if (stmt.target_expr.kind == ExprKind::Unary && stmt.target_expr.op == "*" &&
        stmt.target_expr.children.size() == 1 &&
        stmt.target_expr.children.front().kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.children.front().name;
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
    if (stmt.target_expr.kind == ExprKind::Index && stmt.target_expr.children.size() == 2 &&
        stmt.target_expr.children[0].kind == ExprKind::Name) {
        return indexed_value_type(scope.symbols, scope.locals, target_location,
                                  stmt.target_expr.children[0].name, stmt.target_expr.children[1],
                                  "indexed assignment to unknown local: ");
    }
    if (stmt.target_expr.kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.name;
        if (scope.constants.contains(name)) {
            fail(target_location, "cannot assign to constant: " + name);
        }
        const auto local = scope.locals.find(name);
        if (local == scope.locals.end()) {
            fail(target_location, "assignment to unknown local: " + name);
        }
        return local->second;
    }
    if (stmt.target_expr.kind == ExprKind::Member) {
        if (const std::optional<std::string> path = member_path_from_expr(stmt.target_expr)) {
            return member_path_type(scope.symbols, scope.locals, &target_location, *path,
                                    "assignment through unknown local: ");
        }
        fail(target_location, "unsupported assignment target: " + stmt.target_expr.text);
    }
    if (stmt.target_expr.kind != ExprKind::Unknown && lhs.empty()) {
        fail(target_location, "unsupported assignment target: " + stmt.target_expr.text);
    }
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
    check_local_address_escape(stmt, scope.locals);
    if (stmt.kind == StmtKind::Return) {
        const SourceLocation& value_location = node_location(stmt.location, stmt.value_expr);
        const std::string got = infer_expr_ast(scope, stmt.value_expr, &value_location);
        if (return_type == "void" && got != "void")
            fail(value_location, "void function cannot return " + got);
        if (return_type != "void" && !can_assign_ast(scope, return_type, stmt.value_expr, got)) {
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
        check_condition_type(scope, stmt);
        if (has_expr(stmt.message_expr))
            (void)infer_expr_ast(scope, stmt.message_expr,
                                 &node_location(stmt.location, stmt.message_expr));
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        if (has_expr(stmt.value_expr)) {
            (void)infer_expr_ast(scope, stmt.value_expr,
                                 &node_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::Delete) {
        std::vector<std::string> arg_types;
        if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
            for (const Expr& child : stmt.value_expr.children) {
                arg_types.push_back(
                    infer_expr_ast(scope, child, &node_location(stmt.location, child)));
            }
        } else {
            arg_types.push_back(infer_expr_ast(scope, stmt.value_expr,
                                               &node_location(stmt.location, stmt.value_expr)));
        }
        check_deallocation_args(stmt.location, "delete", arg_types);
        return;
    }
    if (stmt.kind == StmtKind::CppEscape || stmt.kind == StmtKind::Pass)
        return;
    if ((stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) && loop_depth == 0) {
        fail(stmt.location, std::string(statement_kind_name(stmt.kind)) + " outside loop");
    }
    if (stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) {
        return;
    }
    if (stmt.kind == StmtKind::If) {
        check_condition_type(scope, stmt);
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        check_condition_type(scope, stmt);
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
        if (!stmt.condition.empty())
            fail(stmt.location, "expected except binding as name: Type");
        if (!stmt.name.empty()) {
            check_local_binding_name(stmt.location, stmt.name);
            if (!known_type(scope.symbols, stmt.type))
                fail(node_location(stmt.location, stmt.type_ref),
                     "unknown catch type: " + stmt.type);
            nested.locals[stmt.name] = "&const[" + stmt.type + "]";
        }
        check_block(nested, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::While) {
        check_condition_type(scope, stmt);
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
        const std::string target_type = assign_target_type(scope, stmt, "");
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
        if (has_expr(stmt.value_expr)) {
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
        if (const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
            !names.empty()) {
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
        if (stmt.target_expr.kind == ExprKind::TupleLiteral) {
            fail(node_location(stmt.location, stmt.target_expr),
                 "tuple destructuring targets must be names");
        }
        if (stmt.target_expr.kind == ExprKind::Name &&
            !scope.locals.contains(stmt.target_expr.name)) {
            const std::string& name = stmt.target_expr.name;
            check_local_binding_name(node_location(stmt.location, stmt.target_expr), name);
            const std::string inferred = infer_expr_ast(
                scope, stmt.value_expr, &node_location(stmt.location, stmt.value_expr));
            scope.locals[name] = inferred.empty() ? "auto" : inferred;
            if (is_dudu_all_caps(name)) {
                scope.constants.insert(name);
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
