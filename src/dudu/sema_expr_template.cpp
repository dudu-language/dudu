#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

TypeRef template_pointee_type_ref_from_expr(const Expr& expr, std::vector<TypeRef> type_args) {
    const std::string name = trim(expr.name.substr(1));
    const TypeKind wrapper = wrapper_type_kind(name);
    TypeRef pointee;
    pointee.kind =
        wrapper != TypeKind::Unknown && type_args.size() == 1 ? wrapper : TypeKind::Template;
    pointee.name = name;
    pointee.text = name + "[" + join_type_ref_texts(type_args) + "]";
    pointee.children = std::move(type_args);
    pointee.location = expr.location;
    pointee.range = expr.range;
    return pointee;
}

} // namespace

std::optional<FunctionSignature> explicit_generic_function_signature_ast(
    const FunctionScope& scope, const Expr& expr, const std::string& callee_base,
    const std::string& emitted_callee, const SourceLocation* location) {
    const auto fn = scope.symbols.function_decls.find(callee_base);
    if (fn == scope.symbols.function_decls.end() || fn->second->generic_params.empty()) {
        return std::nullopt;
    }
    const std::vector<TypeRef> type_args = template_type_refs(expr);
    if (location != nullptr && type_args.size() != fn->second->generic_params.size()) {
        sema_expr_fail(*location, "function " + callee_base + " expects " +
                                      std::to_string(fn->second->generic_params.size()) +
                                      " type arguments, got " + std::to_string(type_args.size()));
    }
    if (location != nullptr) {
        for (const TypeRef& type_arg : type_args) {
            if (const auto unknown = unknown_type_ref(scope.symbols, type_arg)) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : type_arg.location;
                sema_expr_fail(type_location, "unknown generic argument type: " + unknown->first);
            }
        }
    }
    FunctionSignature signature = instantiate_generic_signature(*fn->second, type_args);
    check_call_args_ast(scope, emitted_callee, signature, expr.children, location);
    return signature;
}

std::string infer_template_call_ast(const FunctionScope& scope, const Expr& expr,
                                    const SourceLocation* location) {
    if (expr.template_args.empty() && expr.template_type_args.empty()) {
        if (location != nullptr) {
            sema_expr_fail(*location, "template call expects at least 1 type argument");
        }
        return {};
    }
    const std::string callee = template_call_callee(scope, expr, location);
    const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
    const std::string& callee_base = scoped_callee.key;

    if (starts_with(expr.name, "*")) {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        const size_t arg_count = type_args.size();
        if (location != nullptr && arg_count == 0) {
            sema_expr_fail(*location, "pointer casts expect at least 1 type argument");
        }
        if (location != nullptr && expr.children.size() != 1) {
            sema_expr_fail(*location, "pointer casts expect 1 argument, got " +
                                          std::to_string(expr.children.size()));
        }
        const TypeRef pointee_ref = template_pointee_type_ref_from_expr(expr, type_args);
        if (const auto unknown = unknown_type_ref(scope.symbols, pointee_ref)) {
            if (location != nullptr) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : expr.location;
                sema_expr_fail(type_location, "unknown pointer cast type: " + unknown->first);
            }
            return {};
        }
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return "*" + substitute_type_ref_text(pointee_ref, {});
    }

    const auto allocation = infer_allocation_call(scope.symbols, location, expr.name,
                                                  template_type_refs(expr), expr.children.size());
    if (allocation) {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return *allocation;
    }
    if (expr.name == "sizeof" || expr.name == "alignof") {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        const size_t arg_count = type_args.size();
        if (location != nullptr && arg_count != 1) {
            sema_expr_fail(*location, expr.name + " expects 1 type argument, got " +
                                          std::to_string(arg_count));
        }
        if (arg_count == 1 && location != nullptr) {
            if (const auto unknown = unknown_type_ref(scope.symbols, type_args.front())) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : type_args.front().location;
                sema_expr_fail(type_location, "unknown " + expr.name + " type: " + unknown->first);
            }
        }
        return "usize";
    }
    if (expr.name == "offsetof") {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        const size_t arg_count = type_args.size();
        if (location != nullptr && arg_count != 1) {
            sema_expr_fail(*location,
                           "offsetof expects 1 type argument, got " + std::to_string(arg_count));
        }
        if (location != nullptr && expr.children.size() != 1) {
            sema_expr_fail(*location, "offsetof expects 1 field argument, got " +
                                          std::to_string(expr.children.size()));
        }
        if (location != nullptr && expr.children.size() == 1 &&
            !is_offsetof_field_expr(expr.children.front())) {
            sema_expr_fail(node_location(*location, expr.children.front()),
                           "offsetof field argument must be a field name");
        }
        if (arg_count == 1 && location != nullptr) {
            if (const auto unknown = unknown_type_ref(scope.symbols, type_args.front())) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : type_args.front().location;
                sema_expr_fail(type_location, "unknown offsetof type: " + unknown->first);
            }
        }
        return "usize";
    }

    if (const auto signature =
            explicit_generic_function_signature_ast(scope, expr, callee_base, callee, location)) {
        return signature_return_type_text(*signature);
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
        return callee;
    }

    if (const auto signature = native_signature_for_call(
            scope, callee, expr.children, location, infer_expr_type_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            })) {
        return signature_return_type_text(*signature);
    }
    if (!expr.callee.empty() && expr.callee.front().kind == ExprKind::Member &&
        expr.callee.front().children.size() == 1) {
        const Expr& member = expr.callee.front();
        const Expr& receiver_expr = member.children.front();
        const std::string method_name = member.name + "[" + template_args_lookup_text(expr) + "]";
        FunctionSignature signature;
        if (receiver_expr.kind == ExprKind::Name && receiver_expr.name == "class" &&
            !scope.current_class.empty() &&
            static_method_signature_for_type(scope.symbols, scope.current_class, method_name,
                                             signature, location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_text(signature);
        }
        if (receiver_expr.kind == ExprKind::Name && receiver_expr.name != "class" &&
            !scope.locals.contains(receiver_expr.name) &&
            scope.symbols.classes.contains(receiver_expr.name) &&
            static_method_signature_for_type(scope.symbols, receiver_expr.name, method_name,
                                             signature, location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_text(signature);
        }
        const bool bare_nonlocal_receiver =
            receiver_expr.kind == ExprKind::Name && !scope.locals.contains(receiver_expr.name);
        if (!bare_nonlocal_receiver) {
            const std::string receiver_type =
                member_expr_type(scope.symbols, scope.locals, location, receiver_expr);
            if (receiver_type.empty() || receiver_type == "auto") {
                for (const Expr& arg : expr.children) {
                    check_expr_ast(scope, arg, location);
                }
                return "auto";
            }
            const bool foreign_receiver =
                foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type));
            if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                          foreign_receiver ? nullptr : location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type, method_name);
                if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
                    check_call_args_ast(scope, callee, *match, expr.children, location);
                    return signature_return_type_text(*match);
                }
                check_call_args_ast(scope, callee, signature, expr.children, location);
                return signature_return_type_text(signature);
            }
            if (foreign_receiver) {
                for (const Expr& arg : expr.children) {
                    check_expr_ast(scope, arg, location);
                }
                return "auto";
            }
        }
    }
    const size_t method_dot = callee_base.rfind('.');
    if (known_template_constructor_type(scope, callee)) {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return callee;
    }
    if (location != nullptr && callee_base.find('.') == std::string::npos &&
        is_plain_identifier(callee_base) && !known_type(scope.symbols, callee_base)) {
        sema_expr_fail(*location, "unknown function: " + callee);
    }
    if (method_dot != std::string::npos) {
        const std::string prefix = trim(callee_base.substr(0, method_dot));
        if (native_import_path_prefix(scope.symbols, callee_base)) {
            for (const Expr& arg : expr.children) {
                check_expr_ast(scope, arg, location);
            }
            return "auto";
        }
        if (location != nullptr) {
            sema_expr_fail(*location, "unknown function: " + callee);
        }
    }
    if (!expr.callee.empty() && expr.callee.front().kind != ExprKind::Name &&
        expr.callee.front().kind != ExprKind::Member) {
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported template call expression: " + callee_base);
        }
        return {};
    }
    if (location != nullptr) {
        sema_expr_fail(*location, "unknown template call: " + callee);
    }
    return {};
}

std::string infer_constructor_call_ast(const FunctionScope& scope, const Expr& expr,
                                       const std::string& callee, const SourceLocation* location) {
    if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee));
        klass != scope.symbols.classes.end()) {
        reject_abstract_construction(scope.symbols, callee, location);
        check_constructor_args_ast(
            scope, *klass->second, expr.children, location, infer_expr_type_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            });
        return callee;
    }
    for (const Expr& arg : expr.children) {
        check_expr_ast(scope, arg, location);
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
            sema_expr_fail(*location, message.str());
        }
    };

    if (callee == "len") {
        check_arity(1, 1);
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return "usize";
    }
    if (callee == "range") {
        check_arity(1, 3);
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return "range";
    }
    if (callee == "min" || callee == "max") {
        check_arity(2, 2);
        TypeRef first;
        for (size_t i = 0; i < expr.children.size(); ++i) {
            const TypeRef got = infer_expr_type_ast(scope, expr.children[i], location);
            if (i == 0) {
                first = got;
                continue;
            }
            if (location != nullptr && !can_assign_ast(scope, first, expr.children[i], got)) {
                sema_expr_fail(*location, callee + " argument 2 expects " +
                                              substitute_type_ref_text(first, {}) + ", got " +
                                              substitute_type_ref_text(got, {}));
            }
        }
        return has_type_ref(first) ? substitute_type_ref_text(first, {}) : "auto";
    }
    if (callee == "print") {
        for (const Expr& arg : expr.children) {
            check_expr_ast(scope, arg, location);
        }
        return "void";
    }
    if (is_deallocation_call(callee)) {
        std::vector<TypeRef> types;
        for (const Expr& arg : expr.children) {
            types.push_back(infer_expr_type_ast(scope, arg, location));
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
    const TypeRef type_ref = parse_type_text(callee.substr(1), expr.location);
    const std::string type = substitute_type_ref_text(type_ref, {});
    if (const auto unknown = unknown_type_ref(scope.symbols, type_ref)) {
        if (location != nullptr) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : expr.location;
            sema_expr_fail(error_location, "unknown pointer cast type: " + unknown->first);
        }
        return std::nullopt;
    }
    for (const Expr& arg : expr.children) {
        check_expr_ast(scope, arg, location);
    }
    return "*" + type;
}

} // namespace dudu
