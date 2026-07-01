#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <utility>

namespace dudu {
namespace {

TypeRef template_pointer_cast_type_ref(const Expr& expr) {
    TypeRef pointer = wrapped_type_ref(TypeKind::Pointer, expr_type_ref(expr), expr.location);
    pointer.range = expr.range;
    return pointer;
}

TypeRef pointer_type_ref_from_pointee(TypeRef pointee, SourceLocation location) {
    return wrapped_type_ref(TypeKind::Pointer, std::move(pointee), location);
}

TypeRef template_constructor_type_ref(const Expr& expr, std::string name,
                                      std::vector<TypeRef> type_args) {
    TypeRef type;
    type.kind = TypeKind::Template;
    type.name = std::move(name);
    type.children = std::move(type_args);
    type.location = expr.location;
    type.range = expr.range;
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
            sema_expr_fail(diagnostic_location(*location, expr.children.front()),
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

std::optional<TypeRef> assume_shape_type_ref(const FunctionScope& scope, const Expr& expr,
                                             const SourceLocation* location) {
    if (direct_callee_name(expr) != "assume_shape") {
        return std::nullopt;
    }
    const std::vector<TypeRef> type_args = template_type_refs(expr);
    if (location != nullptr && type_args.size() != 1) {
        sema_expr_fail(*location, "assume_shape expects 1 type argument, got " +
                                      std::to_string(type_args.size()));
    }
    if (location != nullptr && expr.children.size() != 1) {
        sema_expr_fail(*location, "assume_shape expects 1 value argument, got " +
                                      std::to_string(expr.children.size()));
    }
    if (type_args.empty()) {
        return std::nullopt;
    }
    const TypeRef& target = type_args.front();
    if (location != nullptr && target.kind != TypeKind::Shaped) {
        sema_expr_fail(target.location.line > 0 ? target.location : *location,
                       "assume_shape target must include shape metadata");
    }
    if (const auto unknown = unknown_type_ref(scope.symbols, target)) {
        if (location != nullptr) {
            const SourceLocation type_location =
                unknown->second.line > 0 ? unknown->second : target.location;
            sema_expr_fail(type_location, "unknown assume_shape type: " + unknown->first);
        }
        return std::nullopt;
    }
    if (expr.children.size() == 1 && target.kind == TypeKind::Shaped && !target.children.empty()) {
        const Expr& value = expr.children.front();
        const TypeRef got = infer_expr_type_ast(scope, value, location);
        TypeRef got_base = got;
        if (got.kind == TypeKind::Shaped && !got.children.empty()) {
            got_base = got.children.front();
        }
        if (location != nullptr &&
            !can_assign_ast(scope, target.children.front(), value, got_base)) {
            const std::string expected_display =
                substitute_type_ref_text(target.children.front(), {});
            const std::string got_display = substitute_type_ref_text(got, {});
            sema_expr_fail(*location, "assume_shape value expects " + expected_display + ", got " +
                                          got_display);
        }
    }
    return target;
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
            if (location != nullptr && !can_assign_ast(scope, first, expr.children[i], got_ref)) {
                const std::string expected_display = type_ref_text(first);
                const std::string got_display = type_ref_text(got_ref);
                sema_expr_fail(*location, std::string(callee) + " argument " +
                                              std::to_string(i + 1) + " expects " +
                                              expected_display + ", got " + got_display);
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

std::optional<TypeRef> direct_pointer_cast_type_ref(const FunctionScope& scope, const Expr& expr,
                                                    const std::string& callee,
                                                    const SourceLocation* location) {
    if (!starts_with(callee, "*")) {
        return std::nullopt;
    }
    if (!has_expr_type_ref(expr)) {
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported pointer cast expression: " + callee);
        }
        return std::nullopt;
    }
    TypeRef pointee = expr_type_ref(expr);
    if (const auto unknown = unknown_type_ref(scope.symbols, pointee)) {
        if (location != nullptr) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : expr.location;
            sema_expr_fail(error_location, "unknown pointer cast type: " + unknown->first);
        }
        return std::nullopt;
    }
    for (const Expr& arg : expr.children) {
        (void)infer_expr_type_ast(scope, arg, location);
    }
    return pointer_type_ref_from_pointee(std::move(pointee), expr.location);
}

} // namespace

std::optional<TypeRef> direct_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                            const SourceLocation* location) {
    const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
    const std::string& callee = scoped_callee.key;
    if (callee.empty()) {
        if (has_expr_callee(expr) && expr_callee(expr).front().kind == ExprKind::Member) {
            if (const auto method_type = direct_member_call_type_ref(scope, expr, location)) {
                return *method_type;
            }
        }
        return std::nullopt;
    }
    if (const auto pointer_cast = direct_pointer_cast_type_ref(scope, expr, callee, location)) {
        return *pointer_cast;
    }
    if (is_super_call(callee)) {
        return infer_super_call_type_ref(scope, expr, callee, location);
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
        return out;
    }
    if (const auto decl = scope.symbols.function_decls.find(callee);
        decl != scope.symbols.function_decls.end() && !decl->second->generic_params.empty()) {
        if (const auto type_args = infer_generic_call_type_args(scope, *decl->second, callee,
                                                                expr.children, location)) {
            const FunctionSignature signature =
                instantiate_generic_signature(*decl->second, *type_args);
            check_call_args_ast(scope, callee, signature, expr.children, location);
            if (location != nullptr) {
                check_instantiated_generic_function_body(scope, *decl->second, *type_args, "",
                                                         *location);
            }
            return signature_return_type_ref(signature);
        }
        return std::nullopt;
    }
    if (const auto overloads = scope.symbols.function_overload_signatures.find(callee);
        overloads != scope.symbols.function_overload_signatures.end()) {
        if (const auto match = matching_signature_ast(scope, overloads->second, expr.children)) {
            check_call_args_ast(scope, callee, *match, expr.children, location);
            return signature_return_type_ref(*match);
        }
        if (location != nullptr && !overloads->second.empty()) {
            check_call_args_ast(scope, callee, overloads->second.front(), expr.children, location);
        }
        return std::nullopt;
    }
    if (const auto signature = match_native_signature(scope, callee, {}, expr.children, location)) {
        return signature_return_type_ref(*signature);
    }
    if (scope.local_type_refs.contains(callee)) {
        FunctionSignature signature;
        if (parse_local_function_type(scope, callee, signature)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
    }
    if (is_builtin_call(callee)) {
        return direct_builtin_call_type_ref(scope, expr, callee, location);
    }
    const TypeRef callee_type = named_type_ref(callee, expr.location);
    if (known_template_constructor_type(scope, callee_type)) {
        if (const ClassDecl* klass = class_for_receiver_type(scope.symbols, callee_type)) {
            reject_abstract_construction(scope.symbols, callee_type, location);
            check_constructor_args_ast(scope, *klass, expr.children, location);
        } else {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_type_ast(scope, arg, location);
            }
        }
        return callee_type;
    }
    if (const auto method_type = direct_member_call_type_ref(scope, expr, callee, location)) {
        return *method_type;
    }
    return std::nullopt;
}

std::optional<TypeRef> direct_template_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                     const SourceLocation* location) {
    if (!has_expr_template_args(expr) && !has_expr_template_type_args(expr)) {
        if (location != nullptr) {
            sema_expr_fail(*location, "template call expects at least 1 type argument");
        }
        return std::nullopt;
    }
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
        TypeRef pointer = template_pointer_cast_type_ref(expr);
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
    if (const auto assumed = assume_shape_type_ref(scope, expr, location)) {
        return *assumed;
    }
    const std::string callee = template_call_callee(scope, expr, location);
    const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
    const std::string& callee_base = scoped_callee.key;
    if (const auto signature =
            explicit_generic_function_signature_ast(scope, expr, callee_base, callee, location)) {
        return signature_return_type_ref(*signature);
    }
    if (const ClassDecl* klass =
            class_for_receiver_type(scope.symbols, named_type_ref(callee_base, expr.location));
        klass != nullptr && !klass->generic_params.empty()) {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        if (location != nullptr &&
            !generic_arity_matches(klass->generic_params, type_args.size())) {
            sema_expr_fail(*location, "type " + callee_base + " expects " +
                                          std::to_string(generic_min_arity(klass->generic_params)) +
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
        const ClassDecl instantiated = instantiate_generic_class(*klass, type_args, callee);
        const TypeRef constructor_type =
            template_constructor_type_ref(expr, callee_base, type_args);
        reject_abstract_construction(scope.symbols, constructor_type, location);
        check_constructor_args_ast(scope, instantiated, expr.children, location);
        return constructor_type;
    }
    if (const auto signature = match_native_signature(scope, callee_base, template_type_refs(expr),
                                                      expr.children, location)) {
        return signature_return_type_ref(*signature);
    }
    if (const auto method_type =
            direct_template_member_call_type_ref(scope, expr, callee, location)) {
        return *method_type;
    }
    const TypeRef constructor_type =
        template_constructor_type_ref(expr, callee_base, template_type_refs(expr));
    if (known_template_constructor_type(scope, constructor_type)) {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return constructor_type;
    }
    return std::nullopt;
}

} // namespace dudu
