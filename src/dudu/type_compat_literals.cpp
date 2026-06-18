#include "dudu/type_compat_literals.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/type_compat.hpp"

#include <set>

namespace dudu {
namespace {

bool is_numeric_type(const std::string& type) {
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

std::string wrapped_type_arg(const TypeRef& type) {
    if (const auto inner =
            unary_type_child_ref(type, {TypeKind::Const, TypeKind::Atomic, TypeKind::Volatile,
                                        TypeKind::Device, TypeKind::Storage, TypeKind::Shared})) {
        return substitute_type_ref_text(*inner, {});
    }
    return substitute_type_ref_text(type, {});
}

std::string simple_literal_type(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        return "bool";
    case ExprKind::NoneLiteral:
        return "None";
    case ExprKind::ListLiteral:
        return "list";
    case ExprKind::DictLiteral:
        return "dict";
    case ExprKind::SetLiteral:
        return "set";
    case ExprKind::StringLiteral:
        return "str";
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
        return "number";
    default:
        return {};
    }
}

bool literal_assignable_to(const TypeRef& expected, const Expr& expr) {
    const std::string got = simple_literal_type(expr);
    if (got == "number") {
        if (expected.kind == TypeKind::Template && expected.name == "variant") {
            return assignment_type_allowed(expected, expr,
                                           expr.kind == ExprKind::FloatLiteral ? "f64" : "i32");
        }
        return is_numeric_type(wrapped_type_arg(expected));
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

bool result_value_allowed(const TypeRef& expected, const Expr& expr, const std::string& got) {
    const std::vector<TypeRef> parts = template_type_arg_refs(expected, "Result");
    if (parts.size() != 2 || expr.kind != ExprKind::Call || expr.children.size() != 1) {
        return false;
    }
    const TypeRef got_ref = parse_type_text(got, expr.location);
    const std::string callee = direct_callee_name(expr);
    if (callee == "Ok") {
        const std::vector<TypeRef> ok_args = template_type_arg_refs(got_ref, "Ok");
        return ok_args.size() == 1 &&
               assignment_type_allowed(parts[0], expr.children.front(), ok_args.front());
    }
    if (callee == "Err") {
        const std::vector<TypeRef> err_args = template_type_arg_refs(got_ref, "Err");
        return err_args.size() == 1 &&
               assignment_type_allowed(parts[1], expr.children.front(), err_args.front());
    }
    return false;
}

bool option_value_allowed(const TypeRef& expected, const Expr& expr, const std::string& got) {
    const std::vector<TypeRef> parts = template_type_arg_refs(expected, "Option");
    if (parts.size() != 1) {
        return false;
    }
    const std::string inner = substitute_type_ref_text(parts[0], {});
    return expr.kind == ExprKind::NoneLiteral || got == inner ||
           (is_numeric_type(wrapped_type_arg(parts[0])) && simple_literal_type(expr) == "number");
}

} // namespace

bool parsed_expected_literal_assignment_allowed(const TypeRef& expected, const Expr& expr,
                                                const std::string& got) {
    return container_literal_allowed(expected, expr) || option_value_allowed(expected, expr, got) ||
           result_value_allowed(expected, expr, got);
}

} // namespace dudu
