#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_cpp_escape_calls.hpp"
#include "dudu/sema_expr_internal.hpp"
#include "dudu/sema_methods_internal.hpp"

#include <utility>

namespace dudu {
namespace {

TypeRef cpp_escape_member_path_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const std::string& path);

TypeRef cpp_escape_member_expr_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const Expr& expr);

bool known_cpp_escape_type_spelling(const Symbols& symbols, const std::string& type) {
    if (starts_with(trim(type), "fn(")) {
        return true;
    }
    return known_type_ref(symbols, parse_type_text(type));
}

std::string render_type(const TypeRef& type) {
    return has_type_ref(type) ? substitute_type_ref_text(type, {}) : std::string{};
}

std::optional<TypeRef> infer_parsed_index_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                     const SourceLocation* location) {
    if (location == nullptr || parsed.kind != ExprKind::Index || parsed.children.size() != 2) {
        return std::nullopt;
    }
    const Expr& receiver = parsed.children[0];
    const Expr& index_expr = parsed.children[1];
    if (receiver.kind == ExprKind::Name && !receiver.name.empty()) {
        const TypeRef name_type = local_type_ref(scope, receiver.name, *location);
        if (has_type_ref(name_type)) {
            if (const auto signature = dudu_operator_signature(scope.symbols, "[]", name_type)) {
                check_call_args_ast(scope, receiver.name + "[]", *signature,
                                    index_arg_exprs(index_expr), location);
            }
        }
        return indexed_value_type_ref(scope.symbols, scope.local_type_refs, *location,
                                      receiver.name, index_expr,
                                      "indexed access to unknown local: ");
    }
    if (const auto path = expr_path_from_expr(receiver)) {
        const std::string name = render_expr_path(*path);
        const TypeRef receiver_type = cpp_escape_member_expr_type_ref(scope, location, receiver);
        if (has_type_ref(receiver_type)) {
            return indexed_type_ref_from_type(scope.symbols, *location, receiver_type, index_expr,
                                              name);
        }
    }
    return std::nullopt;
}

TypeRef cpp_escape_member_expr_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const Expr& expr) {
    if (expr.kind == ExprKind::Unknown) {
        return {};
    }
    return member_expr_type_ref(scope.symbols, scope.local_type_refs, location, expr);
}

TypeRef cpp_escape_member_path_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const std::string& path) {
    const SourceLocation parse_location = location == nullptr ? SourceLocation{} : *location;
    const Expr expr = parse_expr_text(path, parse_location);
    return cpp_escape_member_expr_type_ref(scope, location, expr);
}

TypeRef pointer_type_ref(TypeRef pointee, SourceLocation location) {
    return wrapped_type_ref(TypeKind::Pointer, std::move(pointee), location);
}

std::optional<TypeRef> infer_parsed_pointer_cast_escape_ref(const FunctionScope& scope,
                                                            const Expr& parsed,
                                                            const SourceLocation* location) {
    if (parsed.kind != ExprKind::Call || parsed.callee.size() != 1 ||
        parsed.callee.front().kind != ExprKind::Name ||
        !starts_with(parsed.callee.front().name, "*")) {
        return std::nullopt;
    }
    TypeRef type_ref =
        parse_type_text(parsed.callee.front().name.substr(1), parsed.callee.front().location);
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
    return pointer_type_ref(std::move(type_ref), parsed.callee.front().location);
}

std::optional<TypeRef> infer_parsed_unary_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                     const SourceLocation* location) {
    if (parsed.kind != ExprKind::Unary || parsed.children.size() != 1) {
        return std::nullopt;
    }
    const Expr& child = parsed.children.front();
    if (parsed.op == "*" && child.kind == ExprKind::Name) {
        const TypeRef type =
            local_type_ref(scope, child.name, location == nullptr ? child.location : *location);
        if (type.kind == TypeKind::Pointer && !type.children.empty()) {
            return type.children.front();
        }
        return std::nullopt;
    }
    if (parsed.op == "&") {
        const TypeRef type = infer_expr_type_ast(scope, child, location);
        if (has_type_ref(type)) {
            return pointer_type_ref(type, location == nullptr ? child.location : *location);
        }
    }
    return std::nullopt;
}

std::optional<TypeRef> infer_parsed_template_escape_ref(const FunctionScope& scope,
                                                        const Expr& parsed,
                                                        const SourceLocation* location) {
    if (parsed.kind != ExprKind::TemplateCall) {
        return std::nullopt;
    }
    const std::string callee = direct_callee_name(parsed);
    if (const auto allocation = infer_allocation_call_type_ref(
            scope.symbols, location, callee, template_type_refs(parsed), parsed.children.size())) {
        for (const Expr& arg : parsed.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return *allocation;
    }
    return std::nullopt;
}

std::optional<std::string> infer_parsed_name_escape(const FunctionScope& scope, const Expr& parsed,
                                                    const SourceLocation* location) {
    if (parsed.kind != ExprKind::Name) {
        return std::nullopt;
    }
    const SourceLocation name_location = location == nullptr ? parsed.location : *location;
    if (parsed.name == "None") {
        return "None";
    }
    if (const TypeRef local = local_type_ref(scope, parsed.name, name_location);
        has_type_ref(local)) {
        return substitute_type_ref_text(local, {});
    }
    if (const auto fn = scope.symbols.function_signatures.find(parsed.name);
        fn != scope.symbols.function_signatures.end()) {
        return function_type(fn->second);
    }
    if (const auto value = scope.symbols.native_value_type_refs.find(parsed.name);
        value != scope.symbols.native_value_type_refs.end()) {
        return substitute_type_ref_text(value->second, {});
    }
    if (const auto native = scope.symbols.native_function_signatures.find(parsed.name);
        native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
        return function_type(native->second.front());
    }
    if (is_dudu_all_caps(parsed.name)) {
        return "i32";
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
    if (parsed_expr.kind == ExprKind::DictLiteral)
        return "dict";
    if (parsed_expr.kind == ExprKind::SetLiteral)
        return "set";
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
        if (const auto type = infer_parsed_pointer_cast_escape_ref(scope, parsed_expr, location)) {
            return render_type(*type);
        }
    }
    if (const auto type = infer_parsed_unary_escape_ref(scope, parsed_expr, location)) {
        return render_type(*type);
    }
    if (const auto type = infer_parsed_template_escape_ref(scope, parsed_expr, location)) {
        return render_type(*type);
    }
    std::optional<EscapeCall> call_info = parsed_escape_call(parsed_expr);
    if (!call_info) {
        const size_t call = find_call_open(expr);
        if (call != std::string::npos) {
            call_info = escape_call_from_text(expr, call,
                                              location == nullptr ? SourceLocation{} : *location);
        }
    }
    if (call_info) {
        const std::string& callee = call_info->callee;
        const std::vector<Expr>& args = call_info->args;
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
            known_cpp_escape_type_spelling(scope.symbols, callee)) {
            return callee;
        }
        if (scope.local_type_refs.contains(callee)) {
            FunctionSignature signature;
            if (parse_local_function_type(scope, callee, signature)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_text(signature);
            }
        }
        if (const auto member = parsed_member_call(*call_info)) {
            FunctionSignature signature;
            if (!scope.local_type_refs.contains(member->receiver) &&
                scope.symbols.classes.contains(member->receiver) &&
                static_method_signature_for_type(
                    scope.symbols, named_type_ref(member->receiver, member->receiver_expr.location),
                    member->method, signature, location)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_text(signature);
            }
            const TypeRef receiver_type_ref =
                cpp_escape_member_expr_type_ref(scope, nullptr, member->receiver_expr);
            if (scope.local_type_refs.contains(member->receiver) &&
                foreign_cpp_type_name(scope.symbols, receiver_type_ref)) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return "auto";
            }
            if (method_signature_for_type(scope.symbols, receiver_type_ref, member->method,
                                          signature, location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type_ref, member->method);
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
        const size_t method_dot = callee.rfind('.');
        if (method_dot != std::string::npos && is_member_path(callee)) {
            const std::string receiver = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            FunctionSignature signature;
            if (!scope.local_type_refs.contains(receiver) &&
                scope.symbols.classes.contains(receiver) &&
                static_method_signature_for_type(
                    scope.symbols,
                    named_type_ref(receiver, location == nullptr ? SourceLocation{} : *location),
                    method_name, signature, location)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_text(signature);
            }
            const TypeRef receiver_type_ref =
                cpp_escape_member_path_type_ref(scope, nullptr, receiver);
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
            !known_cpp_escape_type_spelling(scope.symbols, callee) && !is_builtin_call(callee)) {
            if (is_dudu_all_caps(callee))
                return "auto";
            sema_expr_fail(*location, "unknown function: " + callee);
        }
    }
    if (parsed_expr.kind == ExprKind::TupleLiteral) {
        return substitute_type_ref_text(infer_expr_type_ast(scope, parsed_expr, location), {});
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
    if (parsed_expr.kind == ExprKind::BoolLiteral)
        return "bool";
    if (parsed_expr.kind == ExprKind::StringLiteral)
        return "str";
    if (parsed_expr.kind == ExprKind::IntLiteral) {
        return "i32";
    }
    if (parsed_expr.kind == ExprKind::FloatLiteral) {
        return "f64";
    }
    if (parsed_expr.kind == ExprKind::NoneLiteral) {
        return "None";
    }
    if (parsed_expr.kind == ExprKind::Unary && parsed_expr.op == "not") {
        return substitute_type_ref_text(infer_expr_type_ast(scope, parsed_expr, location), {});
    }
    if (parsed_expr.kind == ExprKind::Binary) {
        return substitute_type_ref_text(infer_expr_type_ast(scope, parsed_expr, location), {});
    }
    if (const auto indexed = infer_parsed_index_escape_ref(scope, parsed_expr, location)) {
        return render_type(*indexed);
    }
    if (parsed_expr.kind == ExprKind::Member) {
        const TypeRef type = cpp_escape_member_expr_type_ref(scope, location, parsed_expr);
        if (has_type_ref(type)) {
            return render_type(type);
        }
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos && is_member_path(expr)) {
        return render_type(cpp_escape_member_path_type_ref(scope, location, expr));
    }
    if (const auto type = infer_parsed_name_escape(scope, parsed_expr, location)) {
        return *type;
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

TypeRef infer_cpp_escape_expr_ref(const FunctionScope& scope, std::string expr,
                                  const SourceLocation* location) {
    const std::string inferred = infer_cpp_escape_expr(scope, std::move(expr), location);
    if (inferred.empty()) {
        return {};
    }
    return parse_type_text(inferred, location == nullptr ? SourceLocation{} : *location);
}

} // namespace dudu
