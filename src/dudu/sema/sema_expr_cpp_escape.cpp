#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/sema/sema_expr_cpp_escape_calls.hpp"
#include "dudu/sema/sema_expr_cpp_escape_infer.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <utility>

namespace dudu {
using namespace cpp_escape_infer_detail;

TypeRef infer_cpp_escape_expr_ref(const FunctionScope& scope, std::string expr,
                                  const SourceLocation* location) {
    expr = trim(std::move(expr));
    if (expr.empty()) {
        return void_type_ref(location == nullptr ? SourceLocation{} : *location);
    }
    const SourceLocation parse_location = location == nullptr ? SourceLocation{} : *location;
    const Expr parsed_expr = parse_expr_text(expr, parse_location);
    if (parsed_expr.kind == ExprKind::DictLiteral) {
        return named_type_ref("dict", parsed_expr.location);
    }
    if (parsed_expr.kind == ExprKind::SetLiteral) {
        return named_type_ref("set", parsed_expr.location);
    }
    if (starts_with(expr, "{") && expr.back() == '}') {
        for (const std::string& entry :
             split_cpp_escape_top_level(expr.substr(1, expr.size() - 2))) {
            if (find_cpp_escape_top_level_char(entry, ':') != std::string::npos) {
                return named_type_ref("dict", parsed_expr.location);
            }
        }
        return named_type_ref("set", parsed_expr.location);
    }
    const size_t pointer_cast_call = find_call_open(expr);
    if (pointer_cast_call != std::string::npos &&
        find_call_close(expr, pointer_cast_call) == expr.size() - 1) {
        if (const auto type = infer_parsed_pointer_cast_escape_ref(scope, parsed_expr, location)) {
            return *type;
        }
    }
    if (const auto type = infer_parsed_unary_escape_ref(scope, parsed_expr, location)) {
        return *type;
    }
    if (const auto type = infer_parsed_template_escape_ref(scope, parsed_expr, location)) {
        return *type;
    }
    std::optional<EscapeCall> call_info = parsed_escape_call(parsed_expr);
    if (!call_info) {
        const size_t call = find_call_open(expr);
        if (call != std::string::npos) {
            call_info = parse_cpp_escape_call_text(
                expr, call, location == nullptr ? SourceLocation{} : *location);
        }
    }
    if (call_info) {
        const std::string& callee = call_info->callee;
        const std::vector<Expr>& args = call_info->args;
        if (is_deallocation_call(callee)) {
            std::vector<TypeRef> types;
            for (const Expr& arg : args)
                types.push_back(infer_expr_type_ast(scope, arg, location));
            if (location != nullptr)
                check_deallocation_args(*location, callee, types);
            return void_type_ref(call_info->callee_expr.location);
        }
        if (callee == "Ok" || callee == "Err") {
            if (args.size() != 1 && location != nullptr) {
                sema_expr_fail(*location,
                               callee + " expects 1 argument, got " + std::to_string(args.size()));
            }
            TypeRef result;
            result.kind = TypeKind::Template;
            result.name = callee;
            result.location = call_info->callee_expr.location;
            if (args.size() == 1) {
                result.children.push_back(infer_expr_type_ast(scope, args.front(), location));
            }
            return result;
        }
        const TypeRef callee_type_ref = call_info->callee_type_ref;
        if (const ClassDecl* klass = class_for_receiver_type(scope.symbols, callee_type_ref)) {
            check_constructor_args_ast(scope, *klass, args, location);
            return callee_type_ref;
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args_ast(scope, callee, fn->second, args, location);
            return signature_return_type_ref(fn->second);
        }
        const std::vector<TypeRef> explicit_template_args =
            callee_type_ref.kind == TypeKind::Template ? callee_type_ref.children
                                                       : std::vector<TypeRef>{};
        if (const auto signature =
                match_native_signature(scope, callee, explicit_template_args, args, location)) {
            return signature_return_type_ref(*signature);
        }
        if (!is_local_member_call(scope, callee) && callee.find('.') == std::string::npos &&
            known_cpp_escape_type_ref(scope.symbols, callee_type_ref)) {
            return callee_type_ref;
        }
        if (scope.local_type_refs.contains(callee)) {
            FunctionSignature signature;
            if (parse_local_function_type(scope, callee, signature)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_ref(signature);
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
                return signature_return_type_ref(signature);
            }
            const TypeRef receiver_type_ref =
                cpp_escape_member_expr_type_ref(scope, nullptr, member->receiver_expr);
            if (scope.local_type_refs.contains(member->receiver) &&
                foreign_cpp_type_name(scope.symbols, receiver_type_ref)) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return named_type_ref("auto", call_info->callee_expr.location);
            }
            if (method_signature_for_type(scope.symbols, receiver_type_ref, member->method,
                                          signature, location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type_ref, member->method);
                if (const auto match = matching_signature_ast(scope, signatures, args)) {
                    check_call_args_ast(scope, callee, *match, args, location);
                    return signature_return_type_ref(*match);
                }
                if (foreign_cpp_type_name(scope.symbols, receiver_type_ref)) {
                    for (const Expr& arg : args) {
                        (void)infer_expr_type_ast(scope, arg, location);
                    }
                    return named_type_ref("auto", call_info->callee_expr.location);
                }
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_ref(signature);
            }
        }
        const size_t method_dot = callee.rfind('.');
        if (method_dot != std::string::npos && is_cpp_escape_member_path_string(callee)) {
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
                return signature_return_type_ref(signature);
            }
            const TypeRef receiver_type_ref =
                cpp_escape_member_path_type_ref(scope, nullptr, receiver);
            if (scope.local_type_refs.contains(receiver) &&
                foreign_cpp_type_name(scope.symbols, receiver_type_ref)) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return named_type_ref("auto", call_info->callee_expr.location);
            }
            if (method_signature_for_type(scope.symbols, receiver_type_ref, method_name, signature,
                                          location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type_ref, method_name);
                if (const auto match = matching_signature_ast(scope, signatures, args)) {
                    check_call_args_ast(scope, callee, *match, args, location);
                    return signature_return_type_ref(*match);
                }
                if (foreign_cpp_type_name(scope.symbols, receiver_type_ref)) {
                    for (const Expr& arg : args) {
                        (void)infer_expr_type_ast(scope, arg, location);
                    }
                    return named_type_ref("auto", call_info->callee_expr.location);
                }
                check_call_args_ast(scope, callee, signature, args, location);
                return signature_return_type_ref(signature);
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
                    return signature_return_type_ref(signature);
                }
            }
            if (has_type_ref(prefix_type_ref) &&
                foreign_cpp_type_name(scope.symbols, prefix_type_ref)) {
                for (const Expr& arg : args) {
                    (void)infer_expr_type_ast(scope, arg, location);
                }
                return named_type_ref("auto", call_info->callee_expr.location);
            }
        }
        if (location != nullptr && callee.find('.') == std::string::npos &&
            callee.find('[') == std::string::npos && is_plain_identifier(callee) &&
            !known_cpp_escape_type_ref(scope.symbols, callee_type_ref) &&
            !is_builtin_call(callee)) {
            if (is_dudu_all_caps(callee)) {
                return named_type_ref("auto", call_info->callee_expr.location);
            }
            sema_expr_fail(*location, "unknown function: " + callee);
        }
    }
    if (parsed_expr.kind == ExprKind::TupleLiteral) {
        return infer_expr_type_ast(scope, parsed_expr, location);
    }
    const std::vector<std::string> tuple_parts = split_cpp_escape_top_level(expr);
    if (tuple_parts.size() > 1) {
        TypeRef tuple;
        tuple.kind = TypeKind::Template;
        tuple.name = "tuple";
        tuple.location = parsed_expr.location;
        for (size_t i = 0; i < tuple_parts.size(); ++i) {
            tuple.children.push_back(infer_cpp_escape_expr_ref(scope, tuple_parts[i], location));
        }
        return tuple;
    }
    if (parsed_expr.kind == ExprKind::BoolLiteral) {
        return named_type_ref("bool", parsed_expr.location);
    }
    if (parsed_expr.kind == ExprKind::StringLiteral) {
        return named_type_ref("str", parsed_expr.location);
    }
    if (parsed_expr.kind == ExprKind::IntLiteral) {
        return named_type_ref("i32", parsed_expr.location);
    }
    if (parsed_expr.kind == ExprKind::FloatLiteral) {
        return named_type_ref("f64", parsed_expr.location);
    }
    if (parsed_expr.kind == ExprKind::NoneLiteral) {
        return named_type_ref("None", parsed_expr.location);
    }
    if (parsed_expr.kind == ExprKind::Unary && parsed_expr.op == "not") {
        return infer_expr_type_ast(scope, parsed_expr, location);
    }
    if (parsed_expr.kind == ExprKind::Binary) {
        return infer_expr_type_ast(scope, parsed_expr, location);
    }
    if (const auto indexed = infer_parsed_index_escape_ref(scope, parsed_expr, location)) {
        return *indexed;
    }
    if (parsed_expr.kind == ExprKind::Member) {
        const TypeRef type = cpp_escape_member_expr_type_ref(scope, location, parsed_expr);
        if (has_type_ref(type)) {
            return type;
        }
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos && is_cpp_escape_member_path_string(expr)) {
        return cpp_escape_member_path_type_ref(scope, location, expr);
    }
    if (const auto type = infer_parsed_name_escape_ref(scope, parsed_expr, location)) {
        return *type;
    }
    if (const TypeRef local =
            local_type_ref(scope, expr, location == nullptr ? SourceLocation{} : *location);
        has_type_ref(local)) {
        return local;
    }
    if (const auto fn = scope.symbols.function_signatures.find(expr);
        fn != scope.symbols.function_signatures.end()) {
        return function_type_ref(fn->second, parse_location);
    }
    if (const auto value = scope.symbols.native_value_type_refs.find(expr);
        value != scope.symbols.native_value_type_refs.end()) {
        return value->second;
    }
    if (const auto native = scope.symbols.native_function_signatures.find(expr);
        native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
        return function_type_ref(native->second.front(), parse_location);
    }
    if (is_dudu_all_caps(expr)) {
        return named_type_ref("i32", parse_location);
    }
    if (location != nullptr && is_plain_identifier(expr)) {
        throw CompileError(*location, "unknown identifier: " + expr, "dudu.sema.unknown_identifier",
                           expr);
    }
    return {};
}

} // namespace dudu
