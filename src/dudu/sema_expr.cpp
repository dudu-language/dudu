#include "dudu/sema_expr.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

#include <optional>

namespace dudu {
namespace {

std::string index_receiver_label(const Expr& receiver) {
    const std::string label = expr_label(receiver);
    return label.empty() ? "indexed expression" : label;
}

std::optional<std::string> unknown_module_function_message(const Symbols& symbols,
                                                           const std::string& callee) {
    const size_t dot = callee.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= callee.size()) {
        return std::nullopt;
    }
    const std::string prefix = callee.substr(0, dot);
    if (!symbols.module_import_prefixes.contains(prefix)) {
        return std::nullopt;
    }
    return "module '" + prefix + "' has no exported function '" + callee.substr(dot + 1) + "'";
}

} // namespace

[[noreturn]] void sema_expr_fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message, "dudu.sema.expression");
}

TypeRef infer_expr_type_ast(const FunctionScope& scope, const Expr& expr,
                            const SourceLocation* location) {
    const SourceLocation type_location = location == nullptr
                                             ? diagnostic_location(expr.location, expr)
                                             : diagnostic_location(*location, expr);
    switch (expr.kind) {
    case ExprKind::Missing:
        return {};
    case ExprKind::Unknown:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported expression: " + display_expr(expr));
        }
        return {};
    case ExprKind::BoolLiteral:
        return named_type_ref("bool", type_location);
    case ExprKind::IntLiteral:
        return named_type_ref("i32", type_location);
    case ExprKind::FloatLiteral:
        return named_type_ref("f64", type_location);
    case ExprKind::StringLiteral:
        return named_type_ref("str", type_location);
    case ExprKind::NoneLiteral:
        return named_type_ref("None", type_location);
    case ExprKind::ListLiteral:
        return named_type_ref("list", type_location);
    case ExprKind::DictLiteral:
        return named_type_ref("dict", type_location);
    case ExprKind::DictEntry:
        return named_type_ref("auto", type_location);
    case ExprKind::NamedArg:
        return expr.children.size() == 1
                   ? infer_expr_type_ast(scope, expr.children.front(), location)
                   : named_type_ref("auto", type_location);
    case ExprKind::Slice:
        if (location != nullptr) {
            sema_expr_fail(*location, "slice expression must be used inside an index");
        }
        for (const Expr& child : expr.children) {
            if (!missing_expr(child)) {
                check_expr_ast(scope, child, location);
            }
        }
        return named_type_ref("slice", type_location);
    case ExprKind::SetLiteral:
        return named_type_ref("set", type_location);
    case ExprKind::Name:
        if (scope.symbols.generic_params.contains(expr.name)) {
            if (location != nullptr) {
                throw CompileError(*location, "type parameter used as a value: " + expr.name,
                                   "dudu.sema.generic_value", expr.name);
            }
            return {};
        }
        if (const TypeRef* local = local_type_ref_ptr(scope, expr.name)) {
            TypeRef out = *local;
            out.location = type_location;
            return out;
        }
        if (const auto fn = scope.symbols.function_signatures.find(expr.name);
            fn != scope.symbols.function_signatures.end()) {
            return function_type_ref(fn->second, type_location);
        }
        if (const auto value = scope.symbols.native_value_type_refs.find(expr.name);
            value != scope.symbols.native_value_type_refs.end()) {
            return value->second;
        }
        if (const auto native = scope.symbols.native_function_signatures.find(expr.name);
            native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
            return function_type_ref(native->second.front(), type_location);
        }
        if (is_dudu_all_caps(expr.name)) {
            return named_type_ref("i32", type_location);
        }
        if (location != nullptr) {
            throw CompileError(*location, "unknown identifier: " + expr.name,
                               "dudu.sema.unknown_identifier", expr.name);
        }
        return {};
    case ExprKind::TupleLiteral: {
        TypeRef tuple;
        tuple.kind = TypeKind::Template;
        tuple.name = "tuple";
        tuple.location = expr.location;
        tuple.range = expr.range;
        tuple.children.reserve(expr.children.size());
        for (const Expr& child : expr.children) {
            tuple.children.push_back(infer_expr_type_ast(scope, child, location));
        }
        return tuple;
    }
    case ExprKind::Call:
        if (const auto call_type = direct_call_type_ref(scope, expr, location)) {
            return *call_type;
        }
        if (location != nullptr) {
            const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
            const std::string& callee = scoped_callee.key;
            if (has_expr_callee(expr) && expr_callee(expr).front().kind != ExprKind::Name &&
                expr_callee(expr).front().kind != ExprKind::Member) {
                sema_expr_fail(*location, "unsupported call expression: " +
                                              expr_label(expr_callee(expr).front()));
            }
            if (const auto message = unknown_module_function_message(scope.symbols, callee)) {
                sema_expr_fail(*location, *message);
            }
            sema_expr_fail(*location, "unknown function: " + callee);
        }
        return {};
    case ExprKind::TemplateCall:
        if (const auto call_type = direct_template_call_type_ref(scope, expr, location)) {
            return *call_type;
        }
        if (location != nullptr) {
            const std::string callee = template_call_callee(scope, expr, location);
            const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, location);
            const std::string& callee_base = scoped_callee.key;
            if (callee_base.find('.') == std::string::npos && is_plain_identifier(callee_base) &&
                !known_type_ref(scope.symbols, named_type_ref(callee_base, expr.location))) {
                sema_expr_fail(*location, "unknown function: " + callee);
            }
            if (callee_base.rfind('.') != std::string::npos) {
                if (const auto message =
                        unknown_module_function_message(scope.symbols, callee_base)) {
                    sema_expr_fail(*location, *message);
                }
                sema_expr_fail(*location, "unknown function: " + callee);
            }
            if (has_expr_callee(expr) && expr_callee(expr).front().kind != ExprKind::Name &&
                expr_callee(expr).front().kind != ExprKind::Member) {
                sema_expr_fail(*location, "unsupported template call expression: " +
                                              expr_label(expr_callee(expr).front()));
            }
            sema_expr_fail(*location, "unknown template call: " + callee);
        }
        return {};
    case ExprKind::Unary:
        if (const auto unary_type = unary_expr_type_ref(scope, expr, location)) {
            return *unary_type;
        }
        break;
    case ExprKind::Binary:
        if (const auto binary_type = binary_expr_type_ref(scope, expr, location)) {
            return *binary_type;
        }
        break;
    case ExprKind::Index:
        if (expr.children.size() != 2 || missing_expr(expr.children[0]) ||
            missing_expr(expr.children[1])) {
            if (location != nullptr) {
                sema_expr_fail(*location, "index expression expects receiver and index");
            }
            return {};
        }
        if (expr.children.size() == 2 && !missing_expr(expr.children[0]) &&
            !missing_expr(expr.children[1])) {
            const SourceLocation& index_location = location != nullptr ? *location : expr.location;
            const Expr& receiver = expr.children[0];
            if (receiver.kind == ExprKind::Name) {
                if (scope.local_type_refs.contains(receiver.name)) {
                    const TypeRef receiver_type =
                        local_type_ref(scope, receiver.name, index_location);
                    if (const auto signature =
                            dudu_operator_signature(scope.symbols, "[]", receiver_type)) {
                        check_call_args_ast(scope, receiver.name + "[]", *signature,
                                            index_arg_exprs(expr.children[1]), location);
                    }
                }
                return indexed_value_type_ref(scope.symbols, scope.local_type_refs, index_location,
                                              receiver.name, expr.children[1],
                                              "indexed access to unknown local: ");
            }
            const TypeRef receiver_member_type = member_expr_type_ref(
                scope.symbols, scope.local_type_refs, location, receiver, {}, scope.current_class);
            if (has_type_ref(receiver_member_type)) {
                return indexed_type_ref_from_type(scope.symbols, index_location,
                                                  receiver_member_type, expr.children[1],
                                                  index_receiver_label(receiver));
            }
            const TypeRef receiver_type = infer_expr_type_ast(scope, receiver, location);
            if (has_type_ref(receiver_type)) {
                return indexed_type_ref_from_type(scope.symbols, index_location, receiver_type,
                                                  expr.children[1], index_receiver_label(receiver));
            }
        }
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported index expression: " + expr_label(expr));
        }
        return {};
    case ExprKind::Member:
        if (const auto member_type = member_expr_direct_type_ref(scope, expr, location)) {
            return *member_type;
        }
        return {};
    case ExprKind::DefExpression:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: def expressions");
        }
        return {};
    case ExprKind::Comprehension:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: comprehensions");
        }
        return {};
    case ExprKind::Lambda:
        if (location != nullptr) {
            sema_expr_fail(
                *location,
                "unsupported Python feature: lambda; declare a named function and pass the "
                "function name");
        }
        return {};
    case ExprKind::Conditional:
        if (location != nullptr) {
            sema_expr_fail(
                *location,
                "unsupported Python feature: conditional expressions; use an explicit if "
                "statement");
        }
        return {};
    case ExprKind::Await:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: async");
        }
        return {};
    case ExprKind::Yield:
        if (location != nullptr) {
            sema_expr_fail(*location, "unsupported Python feature: generators");
        }
        return {};
    case ExprKind::CppEscape:
        if (TypeRef inferred = infer_cpp_escape_expr_ref(scope, expr.value, location);
            has_type_ref(inferred)) {
            if (inferred.location.line == 0) {
                inferred.location = type_location;
            }
            return inferred;
        }
        return {};
    }
    return {};
}

void check_expr_ast(const FunctionScope& scope, const Expr& expr, const SourceLocation* location) {
    (void)infer_expr_type_ast(scope, expr, location);
}

} // namespace dudu
