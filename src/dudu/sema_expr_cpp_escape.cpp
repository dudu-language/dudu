#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

std::vector<Expr> parse_escape_exprs(const std::vector<std::string>& exprs,
                                     SourceLocation location) {
    std::vector<Expr> out;
    out.reserve(exprs.size());
    for (const std::string& expr : exprs) {
        out.push_back(parse_expr_text(expr, location));
    }
    return out;
}

std::vector<Expr> call_arg_exprs(std::string expr, size_t open, SourceLocation location) {
    std::string args = trim(expr.substr(open + 1, expr.size() - open - 2));
    return parse_escape_exprs(
        args.empty() ? std::vector<std::string>{} : split_top_level_args(args), location);
}

std::string cpp_escape_member_path_type(const FunctionScope& scope, const SourceLocation* location,
                                        const std::string& path) {
    const SourceLocation parse_location = location == nullptr ? SourceLocation{} : *location;
    const Expr expr = parse_expr_text(path, parse_location);
    if (expr.kind == ExprKind::Unknown) {
        return {};
    }
    return member_expr_type(scope.symbols, scope.locals, location, expr);
}

} // namespace

std::string infer_cpp_escape_expr(const FunctionScope& scope, std::string expr,
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
    const size_t pointer_cast_call = find_call_open(expr);
    if (expr.size() > 1 && expr.front() == '*' && pointer_cast_call != std::string::npos &&
        find_call_close(expr, pointer_cast_call) == expr.size() - 1) {
        const TypeRef type_ref =
            parse_type_text(expr.substr(1, pointer_cast_call - 1),
                            location == nullptr ? SourceLocation{} : *location);
        const std::string type = trim(type_ref.text);
        if (const auto unknown = unknown_type_ref(scope.symbols, type_ref)) {
            if (location != nullptr) {
                const SourceLocation error_location =
                    unknown->second.line > 0 ? unknown->second : *location;
                sema_expr_fail(error_location, "unknown pointer cast type: " + unknown->first);
            }
        } else {
            const std::vector<Expr> args = call_arg_exprs(
                expr, pointer_cast_call, location == nullptr ? SourceLocation{} : *location);
            for (const Expr& arg : args) {
                (void)infer_expr_type_ast(scope, arg, location);
            }
            return "*" + type;
        }
    }
    if (expr.size() > 1 && expr.front() == '*') {
        const std::string name = trim(expr.substr(1));
        if (const TypeRef type =
                local_type_ref(scope, name, location == nullptr ? SourceLocation{} : *location);
            type.kind == TypeKind::Pointer && !type.children.empty()) {
            return type_ref_text(type.children.front());
        }
    }
    if (expr.size() > 1 && expr.front() == '&') {
        const std::string name = trim(expr.substr(1));
        if (const TypeRef type =
                local_type_ref(scope, name, location == nullptr ? SourceLocation{} : *location);
            has_type_ref(type)) {
            TypeRef pointer;
            pointer.kind = TypeKind::Pointer;
            pointer.location = location == nullptr ? SourceLocation{} : *location;
            pointer.children.push_back(type);
            pointer.text = substitute_type_ref_text(pointer, {});
            return pointer.text;
        }
        const std::string value_type = infer_cpp_escape_expr(scope, name, location);
        if (!value_type.empty() && value_type != "void") {
            return "*" + value_type;
        }
    }
    const size_t call = find_call_open(expr);
    if (call != std::string::npos && find_call_close(expr, call) == expr.size() - 1) {
        const std::string callee = trim(expr.substr(0, call));
        const std::vector<Expr> args =
            call_arg_exprs(expr, call, location == nullptr ? SourceLocation{} : *location);
        if (const auto type =
                infer_cpp_escape_allocation_call(scope.symbols, location, callee, args))
            return *type;
        if (is_deallocation_call(callee)) {
            std::vector<TypeRef> types;
            for (const Expr& arg : args)
                types.push_back(infer_expr_type_ast(scope, arg, location));
            if (location != nullptr)
                check_deallocation_args(*location, callee, types);
            return "void";
        }
        if (callee == "Ok" || callee == "Err") {
            if (args.size() != 1 && location != nullptr) {
                sema_expr_fail(*location,
                               callee + " expects 1 argument, got " + std::to_string(args.size()));
            }
            return callee + "[" +
                   (args.size() == 1 ? substitute_type_ref_text(
                                           infer_expr_type_ast(scope, args.front(), location), {})
                                     : "") +
                   "]";
        }
        if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee));
            klass != scope.symbols.classes.end()) {
            check_constructor_args_ast(
                scope, *klass->second, args, location, infer_expr_type_ast,
                [&](const std::string& expected, const Expr& value, const std::string& got) {
                    return can_assign_ast(scope, expected, value, got);
                });
            return callee;
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args_ast(scope, callee, fn->second, args, location);
            return signature_return_type_text(fn->second);
        }
        if (const auto signature = native_signature_for_call(
                scope, callee, args, location, infer_expr_type_ast,
                [&](const std::string& expected, const Expr& value, const std::string& got) {
                    return can_assign_ast(scope, expected, value, got);
                })) {
            return signature_return_type_text(*signature);
        }
        if (!is_local_member_call(scope, callee) && callee.find('.') == std::string::npos &&
            known_type(scope.symbols, callee)) {
            return callee;
        }
        if (scope.locals.contains(callee)) {
            FunctionSignature signature;
            if (parse_local_function_type(scope, callee, signature)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_text(signature);
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
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_text(signature);
            }
            const std::string receiver_type = cpp_escape_member_path_type(scope, nullptr, receiver);
            if (scope.locals.contains(receiver) &&
                foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type))) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return "auto";
            }
            if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                          location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type, method_name);
                if (const auto match = matching_signature_ast(scope, signatures, args)) {
                    check_call_args_ast(scope, callee, *match, args, location);
                    return signature_return_type_text(*match);
                }
                if (foreign_cpp_type_name(scope.symbols,
                                          resolve_alias(scope.symbols, receiver_type))) {
                    for (const Expr& arg : args) {
                        (void)infer_expr_type_ast(scope, arg, location);
                    }
                    return "auto";
                }
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_text(signature);
            }
        }
        if (method_dot != std::string::npos) {
            const std::string prefix = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            if (const auto local = scope.locals.find(prefix); local != scope.locals.end()) {
                FunctionSignature signature;
                if (method_signature_for_type(scope.symbols, local->second, method_name, signature,
                                              nullptr)) {
                    check_call_args_ast(scope, callee, signature, args, location);
                    return signature_return_type_text(signature);
                }
            }
            if (const auto local = scope.locals.find(prefix);
                local != scope.locals.end() &&
                foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, local->second))) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return "auto";
            }
            if (native_import_path_prefix(scope.symbols, callee)) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return "auto";
            }
        }
        if (location != nullptr && callee.find('.') == std::string::npos &&
            callee.find('[') == std::string::npos && is_plain_identifier(callee) &&
            !known_type(scope.symbols, callee) && !is_builtin_call(callee)) {
            if (is_dudu_all_caps(callee))
                return "auto";
            sema_expr_fail(*location, "unknown function: " + callee);
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
            out << infer_cpp_escape_expr(scope, tuple_parts[i], location);
        }
        out << "]";
        return out.str();
    }
    const Expr parsed_expr =
        parse_expr_text(expr, location == nullptr ? SourceLocation{} : *location);
    if (parsed_expr.kind == ExprKind::BoolLiteral) {
        return "bool";
    }
    if (parsed_expr.kind == ExprKind::StringLiteral) {
        return "str";
    }
    if (parsed_expr.kind == ExprKind::Unary && parsed_expr.op == "not") {
        return substitute_type_ref_text(infer_expr_type_ast(scope, parsed_expr, location), {});
    }
    if (parsed_expr.kind == ExprKind::Binary) {
        return substitute_type_ref_text(infer_expr_type_ast(scope, parsed_expr, location), {});
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
                    check_call_args_ast(
                        scope, name + "[]", *signature,
                        parse_escape_exprs(split_top_level_args(index_expr), *location), location);
                }
            }
            return indexed_value_type(scope.symbols, scope.locals, scope.local_type_refs, *location,
                                      name, parse_expr_text(index_expr, *location),
                                      "indexed access to unknown local: ");
        }
        if (is_member_path(name)) {
            const std::string receiver_type = cpp_escape_member_path_type(scope, location, name);
            if (!receiver_type.empty()) {
                return indexed_type_from_type(scope.symbols, *location, receiver_type,
                                              parse_expr_text(index_expr, *location), name);
            }
        }
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos && is_member_path(expr)) {
        return cpp_escape_member_path_type(scope, location, expr);
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
        throw CompileError(*location, "unknown identifier: " + expr, "dudu.sema.unknown_identifier",
                           expr);
    }
    return {};
}

} // namespace dudu
