#include "dudu/ast_expr.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

TypeRef template_pointer_cast_type_ref(const Expr& expr, std::vector<TypeRef> type_args) {
    const std::string callee = direct_callee_name(expr);
    const std::string name = callee.size() > 1 ? callee.substr(1) : "";
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

TypeRef named_type_ref(std::string name, SourceLocation location) {
    TypeRef type;
    type.kind = TypeKind::Named;
    type.name = std::move(name);
    type.location = location;
    type.text = type.name;
    return type;
}

TypeRef template_constructor_type_ref(const Expr& expr, std::string name,
                                      std::vector<TypeRef> type_args) {
    TypeRef type;
    type.kind = TypeKind::Template;
    type.name = std::move(name);
    type.children = std::move(type_args);
    type.location = expr.location;
    type.range = expr.range;
    type.text = substitute_type_ref_text(type, {});
    return type;
}

std::optional<TypeRef> type_shape_builtin_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location) {
    const std::string callee = direct_callee_name(expr);
    if (callee != "sizeof" && callee != "alignof" && callee != "offsetof") {
        return std::nullopt;
    }
    const std::vector<TypeRef> type_args = template_type_refs(expr);
    if (location != nullptr && type_args.size() != 1) {
        sema_expr_fail(*location, callee + " expects 1 type argument, got " +
                                      std::to_string(type_args.size()));
    }
    if (callee == "offsetof") {
        if (location != nullptr && expr.children.size() != 1) {
            sema_expr_fail(*location, "offsetof expects 1 field argument, got " +
                                          std::to_string(expr.children.size()));
        }
        if (location != nullptr && expr.children.size() == 1 &&
            !is_offsetof_field_expr(expr.children.front())) {
            sema_expr_fail(node_location(*location, expr.children.front()),
                           "offsetof field argument must be a field name");
        }
    }
    if (type_args.size() == 1) {
        if (const auto unknown = unknown_type_ref(scope.symbols, type_args.front())) {
            if (location != nullptr) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : type_args.front().location;
                sema_expr_fail(type_location, "unknown " + callee + " type: " + unknown->first);
            }
            return std::nullopt;
        }
    }
    return named_type_ref("usize", expr.location);
}

bool call_arity_between(const Expr& expr, const SourceLocation* location, std::string_view callee,
                        const size_t min, const size_t max) {
    if (expr.children.size() >= min && expr.children.size() <= max) {
        return true;
    }
    if (location != nullptr) {
        sema_expr_fail(*location,
                       std::string(callee) + " expects " +
                           (min == max ? std::to_string(min)
                                       : std::to_string(min) + " to " + std::to_string(max)) +
                           " arguments, got " + std::to_string(expr.children.size()));
    }
    return false;
}

std::optional<TypeRef> direct_builtin_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                    std::string_view callee,
                                                    const SourceLocation* location) {
    if (callee == "len") {
        call_arity_between(expr, location, callee, 1, 1);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return named_type_ref("usize", expr.location);
    }
    if (callee == "range") {
        call_arity_between(expr, location, callee, 1, 3);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return named_type_ref("range", expr.location);
    }
    if (callee == "min" || callee == "max") {
        call_arity_between(expr, location, callee, 2, 2);
        if (expr.children.empty()) {
            return TypeRef{};
        }
        const TypeRef first = infer_expr_type_ast(scope, expr.children.front(), location);
        for (size_t i = 1; i < expr.children.size(); ++i) {
            const TypeRef got_ref = infer_expr_type_ast(scope, expr.children[i], location);
            const std::string got = substitute_type_ref_text(got_ref, {});
            if (location != nullptr && !can_assign_ast(scope, first, expr.children[i], got)) {
                sema_expr_fail(*location, std::string(callee) + " argument " +
                                              std::to_string(i + 1) + " expects " +
                                              substitute_type_ref_text(first, {}) + ", got " + got);
            }
        }
        return first;
    }
    if (callee == "print") {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return void_type_ref(expr.location);
    }
    if (is_deallocation_call(callee)) {
        std::vector<TypeRef> types;
        for (const Expr& arg : expr.children) {
            types.push_back(infer_expr_type_ast(scope, arg, location));
        }
        if (location != nullptr) {
            check_deallocation_args(*location, callee, types);
        }
        return void_type_ref(expr.location);
    }
    return std::nullopt;
}

std::optional<TypeRef> direct_member_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                   const std::string& callee,
                                                   const SourceLocation* location) {
    if (expr.callee.empty() || expr.callee.front().kind != ExprKind::Member ||
        expr.callee.front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr.callee.front();
    const Expr& receiver_expr = member.children.front();
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name == "class" &&
        !scope.current_class.empty()) {
        FunctionSignature signature;
        if (static_method_signature_for_type(scope.symbols, scope.current_class, member.name,
                                             signature, location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
    }
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name != "class" &&
        !scope.locals.contains(receiver_expr.name) &&
        scope.symbols.classes.contains(receiver_expr.name)) {
        FunctionSignature signature;
        if (static_method_signature_for_type(scope.symbols, receiver_expr.name, member.name,
                                             signature, location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
    }
    const bool bare_nonlocal_receiver =
        receiver_expr.kind == ExprKind::Name && !scope.locals.contains(receiver_expr.name);
    if (bare_nonlocal_receiver) {
        return std::nullopt;
    }
    const TypeRef receiver_type_ref = infer_expr_type_ast(scope, receiver_expr, location);
    const std::string receiver_type = substitute_type_ref_text(receiver_type_ref, {});
    if (receiver_type.empty() || receiver_type == "auto") {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return named_type_ref("auto", expr.location);
    }
    const bool foreign_receiver =
        foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type));
    if (!foreign_receiver) {
        if (const auto inferred = inferred_generic_method_signature_for_type(
                scope, receiver_type, member.name, expr.children, location,
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
            check_call_args_ast(scope, callee, *inferred, expr.children, location);
            return signature_return_type_ref(*inferred);
        }
    }
    FunctionSignature signature;
    if (method_signature_for_type(scope.symbols, receiver_type, member.name, signature,
                                  foreign_receiver ? nullptr : location)) {
        const std::vector<FunctionSignature> signatures =
            method_signatures_for_type(scope.symbols, receiver_type, member.name);
        if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
            check_call_args_ast(scope, callee, *match, expr.children, location);
            return signature_return_type_ref(*match);
        }
        check_call_args_ast(scope, callee, signature, expr.children, location);
        return signature_return_type_ref(signature);
    }
    if (foreign_receiver) {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return named_type_ref("auto", expr.location);
    }
    return std::nullopt;
}

} // namespace

std::optional<TypeRef> direct_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                            const SourceLocation* location) {
    const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
    const std::string& callee = scoped_callee.key;
    if (callee.empty()) {
        return std::nullopt;
    }
    if (const auto variant = enum_variant_from_path(scope.symbols, callee)) {
        check_enum_variant_args_ast(scope, *variant->first, *variant->second, expr.children,
                                    location);
        return named_type_ref(variant->first->name, expr.location);
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
    if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
        FunctionSignature signature;
        if (parse_local_function_type(scope, callee, local->second, signature)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
    }
    if (is_builtin_call(callee)) {
        return direct_builtin_call_type_ref(scope, expr, callee, location);
    }
    if (known_template_constructor_type(scope, callee)) {
        if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee));
            klass != scope.symbols.classes.end()) {
            reject_abstract_construction(scope.symbols, callee, location);
            check_constructor_args_ast(
                scope, *klass->second, expr.children, location, infer_expr_type_ast,
                [&](const std::string& expected, const Expr& value, const std::string& got) {
                    return can_assign_ast(scope, expected, value, got);
                });
        } else {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_type_ast(scope, arg, location);
            }
        }
        return named_type_ref(callee, expr.location);
    }
    if (const auto method_type = direct_member_call_type_ref(scope, expr, callee, location)) {
        return *method_type;
    }
    return std::nullopt;
}

std::optional<TypeRef> direct_template_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                     const SourceLocation* location) {
    const std::string direct_callee = direct_callee_name(expr);
    if (direct_callee.starts_with("*")) {
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
    if (const auto allocation =
            infer_allocation_call_type_ref(scope.symbols, location, direct_callee,
                                           template_type_refs(expr), expr.children.size())) {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return *allocation;
    }
    if (const auto builtin_type = type_shape_builtin_type_ref(scope, expr, location)) {
        return *builtin_type;
    }
    const std::string callee = template_call_callee(scope, expr, location);
    const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
    const std::string& callee_base = scoped_callee.key;
    if (const auto signature =
            explicit_generic_function_signature_ast(scope, expr, callee_base, callee, location)) {
        return signature_return_type_ref(*signature);
    }
    if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee_base));
        klass != scope.symbols.classes.end() && !klass->second->generic_params.empty()) {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        if (location != nullptr && type_args.size() != klass->second->generic_params.size()) {
            sema_expr_fail(*location, "type " + callee_base + " expects " +
                                          std::to_string(klass->second->generic_params.size()) +
                                          " type arguments, got " +
                                          std::to_string(type_args.size()));
        }
        if (location != nullptr) {
            for (const TypeRef& type_arg : type_args) {
                if (const auto unknown = unknown_type_ref(scope.symbols, type_arg)) {
                    const SourceLocation type_location =
                        unknown->second.line > 0 ? unknown->second : type_arg.location;
                    sema_expr_fail(type_location,
                                   "unknown generic argument type: " + unknown->first);
                }
            }
        }
        const ClassDecl instantiated = instantiate_generic_class(*klass->second, type_args, callee);
        reject_abstract_construction(scope.symbols, callee_base, location);
        check_constructor_args_ast(
            scope, instantiated, expr.children, location, infer_expr_type_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            });
        return template_constructor_type_ref(expr, callee_base, type_args);
    }
    return std::nullopt;
}

} // namespace dudu
