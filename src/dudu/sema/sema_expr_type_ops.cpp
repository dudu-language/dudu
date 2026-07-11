#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/type_compat.hpp"

#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace dudu {
namespace {

bool is_numeric_literal_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Unary && expr.op == "-" && expr.children.size() == 1) {
        return is_numeric_literal_expr(expr.children.front());
    }
    return expr.kind == ExprKind::IntLiteral || expr.kind == ExprKind::FloatLiteral;
}

bool primitive_numeric_name(std::string_view name) {
    return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "u8" ||
           name == "u16" || name == "u32" || name == "u64" || name == "f32" || name == "f64" ||
           name == "usize" || name == "isize";
}

bool primitive_integer_name(std::string_view name) {
    return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "u8" ||
           name == "u16" || name == "u32" || name == "u64" || name == "usize" || name == "isize";
}

std::optional<std::string_view> primitive_type_name(const TypeRef& type) {
    if ((type.kind != TypeKind::Named && type.kind != TypeKind::Qualified) ||
        !type.children.empty()) {
        return std::nullopt;
    }
    return type.name;
}

bool simple_numeric_type_ref(const TypeRef& type) {
    const std::optional<std::string_view> name = primitive_type_name(type);
    return name.has_value() && primitive_numeric_name(*name);
}

bool simple_integer_type_ref(const TypeRef& type) {
    const std::optional<std::string_view> name = primitive_type_name(type);
    return name.has_value() && primitive_integer_name(*name);
}

bool integer_only_op(std::string_view op) {
    return op == "%" || op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
}

std::optional<TypeRef> simple_numeric_binary_type(std::string_view op, const Expr& left_expr,
                                                  const TypeRef& left_ref, const Expr& right_expr,
                                                  const TypeRef& right_ref) {
    if (!simple_numeric_type_ref(left_ref) || !simple_numeric_type_ref(right_ref)) {
        return std::nullopt;
    }
    if (integer_only_op(op) &&
        (!simple_integer_type_ref(left_ref) || !simple_integer_type_ref(right_ref))) {
        return std::nullopt;
    }
    if (type_ref_is_name(left_ref, right_ref.name)) {
        return left_ref;
    }
    if (is_numeric_literal_expr(left_expr)) {
        return right_ref;
    }
    if (is_numeric_literal_expr(right_expr)) {
        return left_ref;
    }
    return std::nullopt;
}

bool is_numeric_type_ref(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type_ref_head_name(type));
}

bool type_ref_known_non_auto(const TypeRef& type) {
    return has_type_ref(type) && !type_ref_is_auto(type);
}

bool type_ref_is_bool(const TypeRef& type) {
    return type_ref_is_name(type, "bool");
}

bool equality_assignment_allowed(const FunctionScope& scope, std::string_view op,
                                 const Expr& left_expr, const TypeRef& left_ref,
                                 const Expr& right_expr, const TypeRef& right_ref) {
    return (op == "==" || op == "!=") && (can_assign_ast(scope, left_ref, right_expr, right_ref) ||
                                          can_assign_ast(scope, right_ref, left_expr, left_ref));
}

TypeRef pointer_type_ref(TypeRef pointee, SourceLocation location) {
    return wrapped_type_ref(TypeKind::Pointer, std::move(pointee), location);
}

} // namespace

bool is_arithmetic_op(std::string_view op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" || op == "&" ||
           op == "|" || op == "^" || op == "<<" || op == ">>";
}

std::optional<TypeRef> contextual_numeric_binary_type(const FunctionScope& scope,
                                                      const Expr& left_expr,
                                                      const TypeRef& left_ref,
                                                      const Expr& right_expr,
                                                      const TypeRef& right_ref) {
    if (is_numeric_literal_expr(left_expr) && is_numeric_type_ref(scope.symbols, right_ref) &&
        can_assign_ast(scope, right_ref, left_expr, left_ref)) {
        return right_ref;
    }
    if (is_numeric_literal_expr(right_expr) && is_numeric_type_ref(scope.symbols, left_ref) &&
        can_assign_ast(scope, left_ref, right_expr, right_ref)) {
        return left_ref;
    }
    return std::nullopt;
}

std::optional<TypeRef> unary_expr_type_ref(const FunctionScope& scope, const Expr& expr,
                                           const SourceLocation* location) {
    if (expr.kind != ExprKind::Unary) {
        return std::nullopt;
    }
    if (expr.children.empty() || missing_expr(expr.children.front())) {
        if (location != nullptr) {
            sema_expr_fail(*location, "operator " + std::string(expr.op) + " expects an operand");
        }
        return std::nullopt;
    }
    const TypeRef got_ref = infer_expr_type_ast(scope, expr.children.front(), location);
    if (expr.op == "not") {
        if (location != nullptr && type_ref_known_non_auto(got_ref) && !type_ref_is_bool(got_ref)) {
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            sema_expr_fail(*location, "not expects bool, got " + got_display);
        }
        return named_type_ref("bool", expr.location);
    }
    if (expr.op == "-") {
        return got_ref;
    }
    if (expr.op == "~") {
        if (location != nullptr && type_ref_known_non_auto(got_ref) &&
            !type_ref_is_integer(resolve_alias_ref(scope.symbols, got_ref))) {
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            sema_expr_fail(*location, "~ expects integer, got " + got_display);
        }
        return got_ref;
    }
    if (expr.op == "*") {
        if (got_ref.kind == TypeKind::Pointer && got_ref.children.size() == 1) {
            return got_ref.children.front();
        }
        if (location != nullptr && type_ref_known_non_auto(got_ref)) {
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            sema_expr_fail(*location, "cannot dereference non-pointer: " + got_display);
        }
        return std::nullopt;
    }
    if (expr.op == "&") {
        if (!has_type_ref(got_ref)) {
            return std::nullopt;
        }
        if (got_ref.kind == TypeKind::Reference && got_ref.children.size() == 1) {
            return pointer_type_ref(got_ref.children.front(), expr.location);
        }
        return pointer_type_ref(got_ref, expr.location);
    }
    if (location != nullptr) {
        sema_expr_fail(*location, "unsupported unary operator: " + std::string(expr.op));
    }
    return std::nullopt;
}

std::optional<TypeRef> binary_expr_type_ref(const FunctionScope& scope, const Expr& expr,
                                            const SourceLocation* location) {
    if (expr.kind != ExprKind::Binary || expr.children.size() != 2) {
        return std::nullopt;
    }
    if (missing_expr(expr.children[0]) || missing_expr(expr.children[1])) {
        if (location != nullptr) {
            sema_expr_fail(*location,
                           "operator " + std::string(expr.op) + " expects left and right operands");
        }
        return std::nullopt;
    }
    const TypeRef left_ref = infer_expr_type_ast(scope, expr.children[0], location);
    const TypeRef right_ref = infer_expr_type_ast(scope, expr.children[1], location);
    if (expr.op == "and" || expr.op == "or") {
        if (location != nullptr && type_ref_known_non_auto(left_ref) &&
            !type_ref_is_bool(left_ref)) {
            const std::string left_display = substitute_type_ref_text(left_ref, {});
            sema_expr_fail(*location, std::string(expr.op) + " expects bool, got " + left_display);
        }
        if (location != nullptr && type_ref_known_non_auto(right_ref) &&
            !type_ref_is_bool(right_ref)) {
            const std::string right_display = substitute_type_ref_text(right_ref, {});
            sema_expr_fail(*location, std::string(expr.op) + " expects bool, got " + right_display);
        }
        return named_type_ref("bool", expr.location);
    }
    if (is_comparison_op(expr.op)) {
        if (contextual_numeric_binary_type(scope, expr.children[0], left_ref, expr.children[1],
                                           right_ref)) {
            return named_type_ref("bool", expr.location);
        }
        if (const auto signature = binary_operator_signature(scope.symbols, expr.op, left_ref,
                                                             expr.children[1], right_ref)) {
            if (location != nullptr) {
                if (signature_param_count(*signature) != 1) {
                    sema_expr_fail(*location,
                                   "operator " + std::string(expr.op) +
                                       " expects 1 argument, got " +
                                       std::to_string(signature_param_count(*signature)));
                } else if (!can_assign_ast(scope, signature_param_type_ref(*signature, 0),
                                           expr.children[1], right_ref) &&
                           !assignment_type_allowed(signature_param_type_ref(*signature, 0),
                                                    expr.children[1], right_ref) &&
                           !comparison_rhs_allowed(scope.symbols, expr.op, left_ref,
                                                   expr.children[1], right_ref)) {
                    const std::string right_display = substitute_type_ref_text(right_ref, {});
                    sema_expr_fail(*location,
                                   "operator " + std::string(expr.op) + " expects " +
                                       type_ref_text(signature_param_type_ref(*signature, 0)) +
                                       ", got " + right_display);
                }
            }
            if (location != nullptr) {
                check_instantiated_dudu_operator_body(scope, expr.op, left_ref, {expr.children[1]},
                                                      {right_ref}, *location);
            }
            return signature_return_type_ref(*signature);
        }
        if (location != nullptr && has_type_ref(left_ref) && has_type_ref(right_ref) &&
            !comparison_rhs_allowed(scope.symbols, expr.op, left_ref, expr.children[1],
                                    right_ref) &&
            !equality_assignment_allowed(scope, expr.op, expr.children[0], left_ref,
                                         expr.children[1], right_ref)) {
            const std::string left_display = substitute_type_ref_text(left_ref, {});
            const std::string right_display = substitute_type_ref_text(right_ref, {});
            sema_expr_fail(*location, "comparison " + std::string(expr.op) + " expects " +
                                          left_display + ", got " + right_display);
        }
        return named_type_ref("bool", expr.location);
    }
    if (is_arithmetic_op(expr.op)) {
        if (const auto simple = simple_numeric_binary_type(expr.op, expr.children[0], left_ref,
                                                           expr.children[1], right_ref)) {
            return *simple;
        }
        if (const auto contextual = contextual_numeric_binary_type(
                scope, expr.children[0], left_ref, expr.children[1], right_ref)) {
            return *contextual;
        }
    }
    if (const auto signature = binary_operator_signature(scope.symbols, expr.op, left_ref,
                                                         expr.children[1], right_ref)) {
        if (location != nullptr) {
            check_call_args_ast(scope, std::string(expr.op), *signature,
                                std::vector<Expr>{expr.children[1]}, location);
        }
        if (location != nullptr) {
            check_instantiated_dudu_operator_body(scope, expr.op, left_ref, {expr.children[1]},
                                                  {right_ref}, *location);
        }
        return signature_return_type_ref(*signature);
    }
    if (location != nullptr && has_type_ref(left_ref) && has_type_ref(right_ref) &&
        !binary_rhs_allowed(scope.symbols, expr.op, left_ref, expr.children[1], right_ref)) {
        const std::string left_display = substitute_type_ref_text(left_ref, {});
        const std::string right_display = substitute_type_ref_text(right_ref, {});
        sema_expr_fail(*location, "operator " + std::string(expr.op) + " expects " + left_display +
                                      ", got " + right_display);
    }
    if (has_type_ref(left_ref)) {
        return left_ref;
    }
    return has_type_ref(right_ref) ? std::optional<TypeRef>{right_ref} : std::nullopt;
}

} // namespace dudu
