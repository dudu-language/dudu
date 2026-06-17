#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

#include <set>

namespace dudu {
namespace {

bool is_numeric_literal_expr(const Expr& expr) {
    return expr.kind == ExprKind::IntLiteral || expr.kind == ExprKind::FloatLiteral;
}

bool is_numeric_type_name(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(trim(type));
}

bool type_ref_is_name_ref(const TypeRef& type, std::string_view name) {
    return type.kind == TypeKind::Named && type_ref_head_name(type) == name;
}

TypeRef named_type_ref(std::string name, SourceLocation location) {
    TypeRef type;
    type.kind = TypeKind::Named;
    type.name = std::move(name);
    type.location = location;
    type.text = type.name;
    return type;
}

TypeRef pointer_type_ref(TypeRef pointee, SourceLocation location) {
    TypeRef pointer;
    pointer.kind = TypeKind::Pointer;
    pointer.children.push_back(std::move(pointee));
    pointer.location = location;
    pointer.text = substitute_type_ref_text(pointer, {});
    return pointer;
}

} // namespace

bool is_arithmetic_op(const std::string& op) {
    static const std::set<std::string> ops = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>"};
    return ops.contains(op);
}

std::optional<std::string> contextual_numeric_binary_type(const FunctionScope& scope,
                                                          const Expr& left_expr,
                                                          const std::string& left,
                                                          const Expr& right_expr,
                                                          const std::string& right) {
    if (is_numeric_literal_expr(left_expr) && is_numeric_type_name(scope.symbols, right) &&
        can_assign_ast(scope, right, left_expr, left)) {
        return right;
    }
    if (is_numeric_literal_expr(right_expr) && is_numeric_type_name(scope.symbols, left) &&
        can_assign_ast(scope, left, right_expr, right)) {
        return left;
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
            sema_expr_fail(*location, "operator " + expr.op + " expects an operand");
        }
        return std::nullopt;
    }
    const TypeRef got_ref = infer_expr_type_ast(scope, expr.children.front(), location);
    const std::string got = substitute_type_ref_text(got_ref, {});
    if (expr.op == "not") {
        if (location != nullptr && !got.empty() && got != "bool" && got != "auto") {
            sema_expr_fail(*location, "not expects bool, got " + got);
        }
        return named_type_ref("bool", expr.location);
    }
    if (expr.op == "-") {
        return got_ref;
    }
    if (expr.op == "~") {
        if (location != nullptr && !got.empty() && got != "auto" && !is_integer_type(got)) {
            sema_expr_fail(*location, "~ expects integer, got " + got);
        }
        return got_ref;
    }
    if (expr.op == "*") {
        if (got_ref.kind == TypeKind::Pointer && got_ref.children.size() == 1) {
            return got_ref.children.front();
        }
        if (location != nullptr && !got.empty() && got != "auto") {
            sema_expr_fail(*location, "cannot dereference non-pointer: " + got);
        }
        return std::nullopt;
    }
    if (expr.op == "&") {
        return has_type_ref(got_ref)
                   ? std::optional<TypeRef>{pointer_type_ref(got_ref, expr.location)}
                   : std::nullopt;
    }
    if (location != nullptr) {
        sema_expr_fail(*location, "unsupported unary operator: " + expr.op);
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
            sema_expr_fail(*location, "operator " + expr.op + " expects left and right operands");
        }
        return std::nullopt;
    }
    const TypeRef left_ref = infer_expr_type_ast(scope, expr.children[0], location);
    const TypeRef right_ref = infer_expr_type_ast(scope, expr.children[1], location);
    const std::string left = substitute_type_ref_text(left_ref, {});
    const std::string right = substitute_type_ref_text(right_ref, {});
    if (expr.op == "and" || expr.op == "or") {
        if (location != nullptr && !left.empty() && left != "bool") {
            sema_expr_fail(*location, expr.op + " expects bool, got " + left);
        }
        if (location != nullptr && !right.empty() && right != "bool") {
            sema_expr_fail(*location, expr.op + " expects bool, got " + right);
        }
        return named_type_ref("bool", expr.location);
    }
    if (is_comparison_op(expr.op)) {
        if (contextual_numeric_binary_type(scope, expr.children[0], left, expr.children[1],
                                           right)) {
            return named_type_ref("bool", expr.location);
        }
        if (const auto signature =
                binary_operator_signature(scope.symbols, expr.op, left, expr.children[1], right)) {
            if (location != nullptr) {
                if (signature->params.size() != 1) {
                    sema_expr_fail(*location, "operator " + expr.op + " expects 1 argument, got " +
                                                  std::to_string(signature->params.size()));
                } else if (!can_assign_ast(scope, signature->params.front(), expr.children[1],
                                           right)) {
                    sema_expr_fail(*location, "operator " + expr.op + " expects " +
                                                  signature->params.front() + ", got " + right);
                }
                if (!type_ref_is_name_ref(signature_return_type_ref(*signature), "bool")) {
                    sema_expr_fail(*location,
                                   "comparison operator " + expr.op + " must return bool");
                }
            }
            return named_type_ref("bool", expr.location);
        }
        if (location != nullptr && !left.empty() && !right.empty() &&
            !comparison_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
            sema_expr_fail(*location,
                           "comparison " + expr.op + " expects " + left + ", got " + right);
        }
        return named_type_ref("bool", expr.location);
    }
    if (is_arithmetic_op(expr.op)) {
        if (const auto contextual = contextual_numeric_binary_type(scope, expr.children[0], left,
                                                                   expr.children[1], right)) {
            if (*contextual == left) {
                return left_ref;
            }
            if (*contextual == right) {
                return right_ref;
            }
            return parse_type_text(*contextual, expr.location);
        }
    }
    if (const auto signature =
            binary_operator_signature(scope.symbols, expr.op, left, expr.children[1], right)) {
        if (location != nullptr) {
            check_call_args_ast(scope, expr.op, *signature, std::vector<Expr>{expr.children[1]},
                                location);
        }
        return signature_return_type_ref(*signature);
    }
    if (location != nullptr && !left.empty() && !right.empty() &&
        !binary_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
        sema_expr_fail(*location, "operator " + expr.op + " expects " + left + ", got " + right);
    }
    if (has_type_ref(left_ref)) {
        return left_ref;
    }
    return has_type_ref(right_ref) ? std::optional<TypeRef>{right_ref} : std::nullopt;
}

} // namespace dudu
