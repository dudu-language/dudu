#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

TypeRef template_pointer_cast_type_ref(const Expr& expr, std::vector<TypeRef> type_args) {
    const std::string name = expr.name.size() > 1 ? expr.name.substr(1) : "";
    const TypeKind wrapper = wrapper_type_kind(name);
    TypeRef pointee;
    pointee.kind =
        wrapper != TypeKind::Unknown && type_args.size() == 1 ? wrapper : TypeKind::Template;
    pointee.name = name;
    pointee.children = std::move(type_args);
    pointee.location = expr.location;
    pointee.range = expr.range;
    pointee.text = substitute_type_ref_text(pointee, {});

    TypeRef pointer;
    pointer.kind = TypeKind::Pointer;
    pointer.children.push_back(std::move(pointee));
    pointer.location = expr.location;
    pointer.range = expr.range;
    pointer.text = substitute_type_ref_text(pointer, {});
    return pointer;
}

} // namespace

std::optional<TypeRef> direct_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                            const SourceLocation* location) {
    const std::string callee = scoped_call_callee_text(scope, expr, location);
    if (callee.empty()) {
        return std::nullopt;
    }
    if (callee == "Ok" || callee == "Err") {
        if (location != nullptr && expr.children.size() != 1) {
            sema_expr_fail(*location, callee + " expects 1 argument, got " +
                                          std::to_string(expr.children.size()));
        }
        TypeRef out;
        out.kind = TypeKind::Template;
        out.name = callee;
        out.location = expr.location;
        out.range = expr.range;
        if (expr.children.size() == 1) {
            out.children.push_back(infer_expr_type_ast(scope, expr.children.front(), location));
        }
        out.text = substitute_type_ref_text(out, {});
        return out;
    }
    if (const auto decl = scope.symbols.function_decls.find(callee);
        decl != scope.symbols.function_decls.end() && !decl->second->generic_params.empty()) {
        if (const auto type_args = infer_generic_call_type_args(
                scope, *decl->second, callee, expr.children, location,
                {.infer_expr_type =
                     [](const FunctionScope& nested, const Expr& arg,
                        const SourceLocation* arg_location) {
                         return infer_expr_type_ast(nested, arg, arg_location);
                     },
                 .can_assign =
                     [](const FunctionScope& nested, const std::string& expected, const Expr& value,
                        const std::string& got) {
                         return can_assign_ast(nested, expected, value, got);
                     }})) {
            const FunctionSignature signature =
                instantiate_generic_signature(*decl->second, *type_args);
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
        return std::nullopt;
    }
    if (const auto fn = scope.symbols.function_signatures.find(callee);
        fn != scope.symbols.function_signatures.end()) {
        check_call_args_ast(scope, callee, fn->second, expr.children, location);
        return signature_return_type_ref(fn->second);
    }
    if (const auto signature = native_signature_for_call(
            scope, callee, expr.children, location, infer_expr_type_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            })) {
        return signature_return_type_ref(*signature);
    }
    return std::nullopt;
}

std::optional<TypeRef> direct_template_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                     const SourceLocation* location) {
    if (expr.name.starts_with("*")) {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        if (location != nullptr && type_args.empty()) {
            sema_expr_fail(*location, "pointer casts expect at least 1 type argument");
        }
        if (location != nullptr && expr.children.size() != 1) {
            sema_expr_fail(*location, "pointer casts expect 1 argument, got " +
                                          std::to_string(expr.children.size()));
        }
        if (type_args.empty()) {
            return std::nullopt;
        }
        TypeRef pointer = template_pointer_cast_type_ref(expr, type_args);
        const TypeRef& pointee = pointer.children.front();
        if (const auto unknown = unknown_type_ref(scope.symbols, pointee)) {
            if (location != nullptr) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : pointee.location;
                sema_expr_fail(type_location, "unknown pointer cast type: " + unknown->first);
            }
            return std::nullopt;
        }
        for (const Expr& arg : expr.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return pointer;
    }
    if (const auto allocation = infer_allocation_call_type_ref(
            scope.symbols, location, expr.name, template_type_refs(expr), expr.children.size())) {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return *allocation;
    }
    const std::string callee = template_call_callee(scope, expr, location);
    const std::string callee_base = scoped_call_callee_text(scope, expr, location);
    if (const auto signature =
            explicit_generic_function_signature_ast(scope, expr, callee_base, callee, location)) {
        return signature_return_type_ref(*signature);
    }
    return std::nullopt;
}

} // namespace dudu
