#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"
#include "dudu/sema_methods_internal.hpp"

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
    const TypeRef type = member_expr_type_ref(scope.symbols, scope.local_type_refs, location, expr);
    return has_type_ref(type) ? substitute_type_ref_text(type, {}) : std::string{};
}

std::string pointer_type_text(TypeRef pointee, SourceLocation location) {
    TypeRef pointer;
    pointer.kind = TypeKind::Pointer;
    pointer.location = location;
    pointer.children.push_back(std::move(pointee));
    pointer.text = substitute_type_ref_text(pointer, {});
    return pointer.text;
}

std::optional<std::string> infer_parsed_pointer_cast_escape(const FunctionScope& scope,
                                                            const Expr& parsed,
                                                            const SourceLocation* location) {
    if (parsed.kind != ExprKind::Call || parsed.callee.size() != 1 ||
        parsed.callee.front().kind != ExprKind::Name ||
        !starts_with(parsed.callee.front().name, "*")) {
        return std::nullopt;
    }
    TypeRef type_ref = parse_type_text(parsed.callee.front().name.substr(1),
                                       parsed.callee.front().location);
    const std::string type = substitute_type_ref_text(type_ref, {});
    if (const auto unknown = unknown_type_ref(scope.symbols, type_ref)) {
        if (location != nullptr) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : parsed.callee.front().location;
            sema_expr_fail(error_location, "unknown pointer cast type: " + unknown->first);
        }
        return std::nullopt;
    }
    for (const Expr& arg : parsed.children) {
        (void)infer_expr_type_ast(scope, arg, location);
    }
    return "*" + type;
}

std::optional<std::string> infer_parsed_unary_escape(const FunctionScope& scope, const Expr& parsed,
                                                     const SourceLocation* location) {
    if (parsed.kind != ExprKind::Unary || parsed.children.size() != 1) {
        return std::nullopt;
    }
    const Expr& child = parsed.children.front();
    if (parsed.op == "*" && child.kind == ExprKind::Name) {
        const TypeRef type = local_type_ref(
            scope, child.name, location == nullptr ? child.location : *location);
        if (type.kind == TypeKind::Pointer && !type.children.empty()) {
            return type_ref_text(type.children.front());
        }
        return std::nullopt;
    }
    if (parsed.op == "&") {
        const TypeRef type = infer_expr_type_ast(scope, child, location);
        if (has_type_ref(type)) {
            return pointer_type_text(type, location == nullptr ? child.location : *location);
        }
    }
    return std::nullopt;
}

} // namespace

std::string infer_cpp_escape_expr(const FunctionScope& scope, std::string expr,
                                  const SourceLocation* location) {
    expr = trim(std::move(expr));
    if (expr.empty())
        return "void";
    const SourceLocation parse_location = location == nullptr ? SourceLocation{} : *location;
    const Expr parsed_expr = parse_expr_text(expr, parse_location);
    if (starts_with(expr, "{") && expr.back() == '}') {
        for (const std::string& entry : split_top_level(expr.substr(1, expr.size() - 2))) {
            if (find_top_level_char(entry, ':') != std::string::npos)
                return "dict";
        }
        return "set";
    }
    const size_t pointer_cast_call = find_call_open(expr);
    if (pointer_cast_call != std::string::npos &&
        find_call_close(expr, pointer_cast_call) == expr.size() - 1) {
        if (const auto type = infer_parsed_pointer_cast_escape(scope, parsed_expr, location)) {
            return *type;
        }
    }
    if (const auto type = infer_parsed_unary_escape(scope, parsed_expr, location)) {
        return *type;
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
        if (const ClassDecl* klass =
                class_for_receiver_type(scope.symbols, parse_type_text(callee))) {
            check_constructor_args_ast(scope, *klass, args, location);
            return callee;
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args_ast(scope, callee, fn->second, args, location);
            return signature_return_type_text(fn->second);
        }
        if (const auto signature = native_signature_for_call(
                scope, callee, args, location, infer_expr_type_ast,
                [&](const TypeRef& expected, const Expr& value, const TypeRef& got) {
                    return can_assign_ast(scope, expected, value, got);
                })) {
            return signature_return_type_text(*signature);
        }
        if (!is_local_member_call(scope, callee) && callee.find('.') == std::string::npos &&
            known_type(scope.symbols, callee)) {
            return callee;
        }
        if (scope.local_type_refs.contains(callee)) {
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
            if (!scope.local_type_refs.contains(receiver) &&
                scope.symbols.classes.contains(receiver) &&
                static_method_signature_for_type(scope.symbols, parse_type_text(receiver),
                                                 method_name, signature, location)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_text(signature);
            }
            const std::string receiver_type = cpp_escape_member_path_type(scope, nullptr, receiver);
            const TypeRef receiver_type_ref =
                parse_type_text(receiver_type, location == nullptr ? SourceLocation{} : *location);
            if (scope.local_type_refs.contains(receiver) &&
                foreign_cpp_type_name(scope.symbols, receiver_type_ref)) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return "auto";
            }
            if (method_signature_for_type(scope.symbols, receiver_type_ref, method_name, signature,
                                          location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type_ref, method_name);
                if (const auto match = matching_signature_ast(scope, signatures, args)) {
                    check_call_args_ast(scope, callee, *match, args, location);
                    return signature_return_type_text(*match);
                }
                if (foreign_cpp_type_name(scope.symbols, receiver_type_ref)) {
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
            const TypeRef prefix_type_ref =
                local_type_ref(scope, prefix, location == nullptr ? SourceLocation{} : *location);
            if (has_type_ref(prefix_type_ref)) {
                FunctionSignature signature;
                if (method_signature_for_type(scope.symbols, prefix_type_ref, method_name,
                                              signature, nullptr)) {
                    check_call_args_ast(scope, callee, signature, args, location);
                    return signature_return_type_text(signature);
                }
            }
            if (has_type_ref(prefix_type_ref) &&
                foreign_cpp_type_name(scope.symbols, prefix_type_ref)) {
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
            if (const TypeRef name_type =
                    local_type_ref(scope, name, location == nullptr ? SourceLocation{} : *location);
                has_type_ref(name_type)) {
                if (const auto signature =
                        dudu_operator_signature(scope.symbols, "[]", name_type)) {
                    check_call_args_ast(
                        scope, name + "[]", *signature,
                        parse_escape_exprs(split_top_level_args(index_expr), *location), location);
                }
            }
            return substitute_type_ref_text(
                indexed_value_type_ref(scope.symbols, scope.local_type_refs, *location, name,
                                       parse_expr_text(index_expr, *location),
                                       "indexed access to unknown local: "),
                {});
        }
        if (is_member_path(name)) {
            const std::string receiver_type = cpp_escape_member_path_type(scope, location, name);
            if (!receiver_type.empty()) {
                return substitute_type_ref_text(
                    indexed_type_ref_from_type(scope.symbols, *location,
                                               parse_type_text(receiver_type, *location),
                                               parse_expr_text(index_expr, *location), name),
                    {});
            }
        }
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos && is_member_path(expr)) {
        return cpp_escape_member_path_type(scope, location, expr);
    }
    if (const TypeRef local =
            local_type_ref(scope, expr, location == nullptr ? SourceLocation{} : *location);
        has_type_ref(local)) {
        return substitute_type_ref_text(local, {});
    }
    if (const auto fn = scope.symbols.function_signatures.find(expr);
        fn != scope.symbols.function_signatures.end()) {
        return function_type(fn->second);
    }
    if (const auto value = scope.symbols.native_value_type_refs.find(expr);
        value != scope.symbols.native_value_type_refs.end()) {
        return substitute_type_ref_text(value->second, {});
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
