#include "dudu/sema/type_compat_literals.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/type_compat.hpp"

#include <set>

namespace dudu {
namespace {

bool is_numeric_type_name(const std::string& type) {
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

TypeRef wrapped_type_arg(const TypeRef& type) {
    if (const auto inner =
            unary_type_child_ref(type, {TypeKind::Const, TypeKind::Atomic, TypeKind::Volatile,
                                        TypeKind::Device, TypeKind::Storage, TypeKind::Shared})) {
        return *inner;
    }
    return type;
}

bool is_numeric_type(const TypeRef& type) {
    return is_numeric_type_name(type_ref_head_name(wrapped_type_arg(type)));
}

TypeRef simple_literal_type_ref(const Expr& expr) {
    std::string name;
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        name = "bool";
        break;
    case ExprKind::NoneLiteral:
        name = "None";
        break;
    case ExprKind::ListLiteral:
        name = "list";
        break;
    case ExprKind::DictLiteral:
        name = "dict";
        break;
    case ExprKind::SetLiteral:
        name = "set";
        break;
    case ExprKind::StringLiteral:
        name = "str";
        break;
    case ExprKind::IntLiteral:
        name = "i32";
        break;
    case ExprKind::FloatLiteral:
        name = "f64";
        break;
    default:
        return {};
    }
    return named_type_ref(name, expr.location);
}

bool literal_assignable_to(const TypeRef& expected, const Expr& expr) {
    const TypeRef got = simple_literal_type_ref(expr);
    if (expr.kind == ExprKind::IntLiteral || expr.kind == ExprKind::FloatLiteral) {
        if (expected.kind == TypeKind::Template && expected.name == "variant") {
            return assignment_type_allowed(expected, expr, got);
        }
        return is_numeric_type(expected);
    }
    return assignment_type_allowed(expected, expr, got);
}

bool container_literal_allowed(const TypeRef& expected, const Expr& expr) {
    const std::vector<TypeRef> list_args = template_type_arg_refs(expected, "list");
    if (!list_args.empty()) {
        if (expr.kind != ExprKind::ListLiteral || list_args.size() != 1) {
            return false;
        }
        for (const Expr& entry : expr.children) {
            if (!literal_assignable_to(list_args[0], entry)) {
                return false;
            }
        }
        return true;
    }

    const std::vector<TypeRef> set_args = template_type_arg_refs(expected, "set");
    if (!set_args.empty()) {
        if (expr.kind != ExprKind::SetLiteral || set_args.size() != 1) {
            return false;
        }
        for (const Expr& entry : expr.children) {
            if (!literal_assignable_to(set_args[0], entry)) {
                return false;
            }
        }
        return true;
    }

    const std::vector<TypeRef> dict_args = template_type_arg_refs(expected, "dict");
    if (dict_args.empty()) {
        return false;
    }
    if (expr.kind == ExprKind::SetLiteral && expr.children.empty()) {
        return true;
    }
    if (expr.kind != ExprKind::DictLiteral || dict_args.size() != 2) {
        return false;
    }
    for (const Expr& entry : expr.children) {
        if (entry.children.size() != 2 || !literal_assignable_to(dict_args[0], entry.children[0]) ||
            !literal_assignable_to(dict_args[1], entry.children[1])) {
            return false;
        }
    }
    return true;
}

bool result_value_allowed(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    const std::vector<TypeRef> parts = template_type_arg_refs(expected, "Result");
    if (parts.size() != 2 || expr.kind != ExprKind::Call || expr.children.size() != 1) {
        return false;
    }
    const std::string callee = direct_callee_name(expr);
    if (callee == "Ok") {
        const std::vector<TypeRef> ok_args = template_type_arg_refs(got, "Ok");
        return ok_args.size() == 1 &&
               assignment_type_allowed(parts[0], expr.children.front(), ok_args.front());
    }
    if (callee == "Err") {
        const std::vector<TypeRef> err_args = template_type_arg_refs(got, "Err");
        return err_args.size() == 1 &&
               assignment_type_allowed(parts[1], expr.children.front(), err_args.front());
    }
    return false;
}

bool option_value_allowed(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    const std::vector<TypeRef> parts = template_type_arg_refs(expected, "Option");
    if (parts.size() != 1) {
        return false;
    }
    return expr.kind == ExprKind::NoneLiteral || type_ref_equivalent(got, parts[0]) ||
           (is_numeric_type(parts[0]) &&
            (expr.kind == ExprKind::IntLiteral || expr.kind == ExprKind::FloatLiteral));
}

} // namespace

bool parsed_expected_literal_assignment_allowed(const TypeRef& expected, const Expr& expr,
                                                const TypeRef& got) {
    return container_literal_allowed(expected, expr) || option_value_allowed(expected, expr, got) ||
           result_value_allowed(expected, expr, got);
}

} // namespace dudu
