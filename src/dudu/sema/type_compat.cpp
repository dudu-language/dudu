#include "dudu/sema/type_compat.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/shape_value_expr.hpp"
#include "dudu/core/text.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/type_compat_literals.hpp"
#include "dudu/sema/type_compat_native.hpp"
#include "dudu/sema/type_compat_structural.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dudu {
namespace {

bool is_numeric_type_name(const std::string& type) {
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

bool is_integer_type_name(const std::string& type) {
    static const std::set<std::string> integers = {"i8",  "i16", "i32", "i64",   "u8",
                                                   "u16", "u32", "u64", "usize", "isize"};
    return integers.contains(type);
}

TypeRef wrapped_type_arg_ref(const TypeRef& type) {
    if (const auto inner =
            unary_type_child_ref(type, {TypeKind::Const, TypeKind::Atomic, TypeKind::Volatile,
                                        TypeKind::Device, TypeKind::Storage, TypeKind::Shared})) {
        return *inner;
    }
    return type;
}

bool is_numeric_type(const TypeRef& type) {
    return is_numeric_type_name(type_ref_head_name(wrapped_type_arg_ref(type)));
}

bool is_scalar_index_type(const TypeRef& type) {
    return type_ref_is_integer(wrapped_type_arg_ref(type));
}

bool is_basic_index_type(const TypeRef& type) {
    const TypeRef wrapped = wrapped_type_arg_ref(type);
    return is_scalar_index_type(wrapped) || type_ref_is_name(wrapped, "slice") ||
           type_ref_is_name(wrapped, "ellipsis") || type_ref_is_name(wrapped, "new_axis") ||
           type_ref_is_name(wrapped, "scalar_index");
}

bool is_index_category_assignment(const TypeRef& expected, const TypeRef& got) {
    const TypeRef normalized_expected = normalize_cpp_type_artifacts_ref(expected);
    const TypeRef normalized_got = normalize_cpp_type_artifacts_ref(got);
    if (type_ref_is_name(normalized_expected, "scalar_index")) {
        return is_scalar_index_type(normalized_got);
    }
    if (type_ref_is_name(normalized_expected, "basic_index")) {
        return is_basic_index_type(normalized_got);
    }
    return false;
}

std::optional<TypeRef> unary_child_ref(const TypeRef& type, TypeKind kind) {
    if (type.kind != kind || type.children.size() != 1) {
        return std::nullopt;
    }
    return type.children.front();
}

std::optional<TypeRef> pointer_pointee_ref(const TypeRef& type) {
    return unary_child_ref(type, TypeKind::Pointer);
}

std::optional<TypeRef> reference_target_ref(const TypeRef& type) {
    return unary_child_ref(type, TypeKind::Reference);
}

bool is_string_type(const TypeRef& type) {
    const std::string head = type_ref_head_name(normalize_cpp_type_artifacts_ref(type));
    return head == "str" || head == "std.string" || head == "std::string";
}

bool is_cstr_type(const TypeRef& type) {
    return type_ref_head_name(normalize_cpp_type_artifacts_ref(type)) == "cstr";
}

std::string type_tail_name(const TypeRef& type) {
    std::string name = trim_copy(type_ref_head_name(normalize_cpp_type_artifacts_ref(type)));
    const size_t dot = name.find_last_of(".:");
    if (dot != std::string::npos && dot + 1 < name.size()) {
        name = name.substr(dot + 1);
    }
    return name;
}

bool is_native_string_view_type(const TypeRef& type) {
    const TypeRef normalized = normalize_cpp_type_artifacts_ref(type);
    const std::string tail = type_tail_name(normalized);
    if (tail == "string_view") {
        return true;
    }
    return normalized.kind == TypeKind::Template && tail == "basic_string_view";
}

std::optional<TypeRef> call_target_type_ref(const Expr& expr) {
    if (expr.kind != ExprKind::Call && expr.kind != ExprKind::TemplateCall) {
        return std::nullopt;
    }
    const std::optional<ExprPath> path = call_callee_path(expr);
    if (!path) {
        return std::nullopt;
    }
    const std::string callee = render_expr_path(*path);
    if (callee.empty()) {
        return std::nullopt;
    }
    if (expr.kind == ExprKind::TemplateCall && has_expr_template_type_args(expr)) {
        TypeRef type;
        type.kind = TypeKind::Template;
        type.name = callee;
        type.children = expr_template_type_args(expr);
        type.location = expr.location;
        return type;
    }
    return named_type_ref(callee, expr.location);
}

bool is_explicit_cast_to(const TypeRef& expected, const Expr& expr) {
    const std::optional<TypeRef> target = call_target_type_ref(expr);
    if (!target) {
        return false;
    }
    return type_ref_equivalent(normalize_cpp_type_artifacts_ref(*target),
                               normalize_cpp_type_artifacts_ref(expected));
}

bool is_numeric_literal_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Unary && expr.op == "-" && expr.children.size() == 1) {
        return is_numeric_literal_expr(expr.children.front());
    }
    return expr.kind == ExprKind::IntLiteral || expr.kind == ExprKind::FloatLiteral;
}

std::optional<std::string> dyn_shape_arithmetic_expr(const TypeRef& type) {
    if (type.kind == TypeKind::Value && trim_copy(type.value) != "dyn") {
        const std::set<std::string> ids = shape_value_expr_identifiers(type.value);
        if (ids.contains("dyn")) {
            return normalize_shape_value_expr(type.value);
        }
    }
    for (const TypeRef& child : type.children) {
        if (const std::optional<std::string> found = dyn_shape_arithmetic_expr(child)) {
            return found;
        }
    }
    return std::nullopt;
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

bool is_container_literal_expr(const Expr& expr) {
    return expr.kind == ExprKind::ListLiteral || expr.kind == ExprKind::SetLiteral ||
           expr.kind == ExprKind::DictLiteral;
}

bool is_reference_binding(const TypeRef& expected, const TypeRef& got) {
    if (const auto target = reference_target_ref(expected)) {
        const TypeRef target_value = wrapped_type_arg_ref(*target);
        return type_ref_equivalent(normalize_cpp_type_artifacts_ref(target_value),
                                   normalize_cpp_type_artifacts_ref(got)) ||
               (is_string_type(target_value) && is_string_type(got));
    }
    return false;
}

bool is_value_from_reference(const TypeRef& expected, const TypeRef& got) {
    if (const auto target = reference_target_ref(got)) {
        const TypeRef target_value = wrapped_type_arg_ref(*target);
        return type_ref_equivalent(normalize_cpp_type_artifacts_ref(expected),
                                   normalize_cpp_type_artifacts_ref(target_value)) ||
               (is_string_type(expected) && is_string_type(target_value));
    }
    return false;
}

bool is_value_wrapper_assignment(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    const TypeRef inner = wrapped_type_arg_ref(expected);
    if (type_ref_same_shape(inner, expected)) {
        return false;
    }
    return assignment_type_allowed(inner, expr, got);
}

bool is_null_pointer_like(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    return (pointer_pointee_ref(expected).has_value() || expected.kind == TypeKind::Function) &&
           expr.kind == ExprKind::NoneLiteral && type_ref_is_name(got, "None");
}

bool is_null_cstr(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    return is_cstr_type(expected) && expr.kind == ExprKind::NoneLiteral &&
           type_ref_is_name(got, "None");
}

bool is_void_pointer_target(const TypeRef& expected, const TypeRef& got) {
    const auto expected_pointee = pointer_pointee_ref(expected);
    const auto got_pointee = pointer_pointee_ref(got);
    if (!expected_pointee || !got_pointee) {
        return false;
    }
    return type_ref_is_void(wrapped_type_arg_ref(*expected_pointee));
}

bool is_const_pointer_binding(const TypeRef& expected, const TypeRef& got) {
    const auto expected_pointee = pointer_pointee_ref(expected);
    const auto got_pointee = pointer_pointee_ref(got);
    if (!expected_pointee || !got_pointee) {
        return false;
    }
    const auto expected_const_inner = unary_child_ref(*expected_pointee, TypeKind::Const);
    return expected_const_inner &&
           type_ref_equivalent(wrapped_type_arg_ref(*got_pointee), *expected_const_inner);
}

bool is_pointer_to_reference_value(const TypeRef& expected, const TypeRef& got) {
    const auto expected_pointee = pointer_pointee_ref(expected);
    const auto got_pointee = pointer_pointee_ref(got);
    if (!expected_pointee || !got_pointee) {
        return false;
    }
    const auto got_reference_target = reference_target_ref(*got_pointee);
    return got_reference_target && type_ref_equivalent(wrapped_type_arg_ref(*expected_pointee),
                                                       wrapped_type_arg_ref(*got_reference_target));
}

bool is_native_function_pointer(const TypeRef& expected, const TypeRef& got) {
    const auto expected_pointee = pointer_pointee_ref(expected);
    return expected_pointee && type_ref_is_void(wrapped_type_arg_ref(*expected_pointee)) &&
           got.kind == TypeKind::Function;
}

bool is_variant_value(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    if (expected.kind != TypeKind::Template || expected.name != "variant" ||
        expected.children.empty()) {
        return false;
    }
    size_t matches = 0;
    for (const TypeRef& alternative : expected.children) {
        if (assignment_type_allowed(alternative, expr, got)) {
            ++matches;
        }
    }
    return matches == 1;
}

bool is_value_from_const(const TypeRef& expected, const TypeRef& got) {
    const auto inner = unary_child_ref(got, TypeKind::Const);
    return inner.has_value() && type_ref_equivalent(expected, *inner);
}

bool all_unknown_type_refs(const std::vector<TypeRef>& types) {
    return std::ranges::all_of(types,
                               [](const TypeRef& type) { return type.kind == TypeKind::Unknown; });
}

bool native_dependent_type_refs_equivalent(const TypeRef& left, const TypeRef& right) {
    if (left.kind != right.kind || left.name != right.name || left.value != right.value) {
        return false;
    }
    if (left.children.size() != right.children.size()) {
        return (left.children.empty() && all_unknown_type_refs(right.children)) ||
               (right.children.empty() && all_unknown_type_refs(left.children));
    }
    for (size_t i = 0; i < left.children.size(); ++i) {
        if (!native_dependent_type_refs_equivalent(left.children[i], right.children[i])) {
            return false;
        }
    }
    return true;
}

bool contains_canonical_native_type_ref(const TypeRef& type) {
    if (is_canonical_native_type_ref(type)) {
        return true;
    }
    return std::ranges::any_of(type.children, contains_canonical_native_type_ref);
}

bool canonical_native_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    return type_assignment_allowed(expected, got) ||
           (contains_canonical_native_type_ref(expected) &&
            contains_canonical_native_type_ref(got) &&
            native_dependent_type_refs_equivalent(expected, got));
}

bool normalized_type_assignment_allowed(const TypeRef& expected_ref, const TypeRef& got_ref) {
    if (has_type_ref(expected_ref) && has_type_ref(got_ref) &&
        structural_type_assignment_allowed(expected_ref, got_ref)) {
        return true;
    }
    return type_ref_is_auto(expected_ref) || !has_type_ref(got_ref) || type_ref_is_auto(got_ref) ||
           (is_string_type(expected_ref) && is_string_type(got_ref)) ||
           is_void_pointer_target(expected_ref, got_ref) ||
           is_const_pointer_binding(expected_ref, got_ref) ||
           is_pointer_to_reference_value(expected_ref, got_ref) ||
           is_reference_binding(expected_ref, got_ref) ||
           is_index_category_assignment(expected_ref, got_ref) ||
           is_value_from_reference(expected_ref, got_ref) ||
           is_value_from_const(expected_ref, got_ref) ||
           is_native_function_pointer(expected_ref, got_ref) ||
           native_associated_type_assignment_allowed(expected_ref, got_ref);
}

} // namespace

bool type_ref_is_integer(const TypeRef& type) {
    const TypeRef normalized = normalize_cpp_type_artifacts_ref(wrapped_type_arg_ref(type));
    return is_integer_type_name(type_ref_head_name(normalized));
}

bool type_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    const TypeRef normalized_expected_ref = normalize_cpp_type_artifacts_ref(expected);
    const TypeRef normalized_got_ref = normalize_cpp_type_artifacts_ref(got);
    if (has_type_ref(normalized_expected_ref) && has_type_ref(normalized_got_ref) &&
        structural_type_assignment_allowed(normalized_expected_ref, normalized_got_ref)) {
        return true;
    }
    return structural_type_assignment_allowed(expected, got) ||
           is_void_pointer_target(expected, got) || is_const_pointer_binding(expected, got) ||
           is_pointer_to_reference_value(expected, got) || is_reference_binding(expected, got) ||
           is_index_category_assignment(expected, got) || is_value_from_reference(expected, got) ||
           is_value_from_const(expected, got) || is_native_function_pointer(expected, got) ||
           native_associated_type_assignment_allowed(expected, got) ||
           normalized_type_assignment_allowed(normalized_expected_ref, normalized_got_ref);
}

bool type_assignment_allowed(const Symbols& symbols, const TypeRef& expected, const TypeRef& got) {
    const TypeRef resolved_expected = resolve_associated_type_ref(symbols, expected);
    const TypeRef resolved_got = resolve_associated_type_ref(symbols, got);
    const TypeRef canonical_expected = canonical_native_type_ref(symbols, resolved_expected);
    const TypeRef canonical_got = canonical_native_type_ref(symbols, resolved_got);
    if (!type_ref_same_shape(canonical_expected, resolved_expected) &&
        !type_ref_same_shape(canonical_got, resolved_got)) {
        return canonical_native_assignment_allowed(canonical_expected, canonical_got);
    }
    return type_assignment_allowed(resolved_expected, resolved_got);
}

bool assignment_type_allowed(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    const TypeRef normalized_expected_ref = normalize_cpp_type_artifacts_ref(expected);
    const TypeRef normalized_got_ref = normalize_cpp_type_artifacts_ref(got);
    if (!type_ref_same_shape(normalized_expected_ref, expected)) {
        return assignment_type_allowed(normalized_expected_ref, expr, normalized_got_ref);
    }
    if (has_type_ref(normalized_got_ref) && !type_ref_is_auto(normalized_got_ref)) {
        if (structural_type_assignment_allowed(normalized_expected_ref, normalized_got_ref)) {
            return true;
        }
    }
    if (parsed_expected_literal_assignment_allowed(normalized_expected_ref, expr,
                                                   normalized_got_ref)) {
        return true;
    }
    if (is_variant_value(normalized_expected_ref, expr, normalized_got_ref)) {
        return true;
    }
    if (has_type_ref(normalized_got_ref) && !type_ref_is_auto(normalized_got_ref)) {
        if (is_void_pointer_target(normalized_expected_ref, normalized_got_ref) ||
            is_const_pointer_binding(normalized_expected_ref, normalized_got_ref) ||
            is_pointer_to_reference_value(normalized_expected_ref, normalized_got_ref) ||
            is_reference_binding(normalized_expected_ref, normalized_got_ref) ||
            is_index_category_assignment(normalized_expected_ref, normalized_got_ref) ||
            is_value_from_reference(normalized_expected_ref, normalized_got_ref) ||
            is_value_from_const(normalized_expected_ref, normalized_got_ref) ||
            is_native_function_pointer(normalized_expected_ref, normalized_got_ref) ||
            native_associated_type_assignment_allowed(normalized_expected_ref,
                                                      normalized_got_ref)) {
            return true;
        }
    }
    return type_ref_is_auto(normalized_expected_ref) ||
           is_explicit_cast_to(normalized_expected_ref, expr) ||
           (!is_container_literal_expr(expr) && !has_type_ref(normalized_got_ref)) ||
           type_ref_is_auto(normalized_got_ref) ||
           (is_string_type(normalized_expected_ref) && is_string_type(normalized_got_ref)) ||
           is_value_wrapper_assignment(normalized_expected_ref, expr, normalized_got_ref) ||
           is_null_pointer_like(normalized_expected_ref, expr, normalized_got_ref) ||
           is_null_cstr(normalized_expected_ref, expr, normalized_got_ref) ||
           is_void_pointer_target(normalized_expected_ref, normalized_got_ref) ||
           is_const_pointer_binding(normalized_expected_ref, normalized_got_ref) ||
           is_pointer_to_reference_value(normalized_expected_ref, normalized_got_ref) ||
           is_reference_binding(normalized_expected_ref, normalized_got_ref) ||
           is_index_category_assignment(normalized_expected_ref, normalized_got_ref) ||
           is_value_from_reference(normalized_expected_ref, normalized_got_ref) ||
           is_value_from_const(normalized_expected_ref, normalized_got_ref) ||
           is_native_function_pointer(normalized_expected_ref, normalized_got_ref) ||
           native_associated_type_assignment_allowed(normalized_expected_ref, normalized_got_ref) ||
           (is_native_string_view_type(normalized_expected_ref) &&
            expr.kind == ExprKind::StringLiteral) ||
           (is_cstr_type(normalized_expected_ref) && is_string_type(normalized_got_ref) &&
            expr.kind == ExprKind::StringLiteral) ||
           (is_numeric_type(normalized_expected_ref) && is_numeric_literal_expr(expr));
}

bool assignment_type_allowed(const Symbols& symbols, const TypeRef& expected, const Expr& expr,
                             const TypeRef& got) {
    const TypeRef resolved_expected = resolve_associated_type_ref(symbols, expected);
    const TypeRef resolved_got = resolve_associated_type_ref(symbols, got);
    const TypeRef canonical_expected = canonical_native_type_ref(symbols, resolved_expected);
    const TypeRef canonical_got = canonical_native_type_ref(symbols, resolved_got);
    if (!type_ref_same_shape(canonical_expected, resolved_expected) &&
        !type_ref_same_shape(canonical_got, resolved_got)) {
        return assignment_type_allowed(canonical_expected, expr, canonical_got) ||
               canonical_native_assignment_allowed(canonical_expected, canonical_got);
    }
    return assignment_type_allowed(resolved_expected, expr, resolved_got);
}

std::string assignment_type_display(const Expr& expr, const TypeRef& got) {
    if (has_type_ref(got)) {
        return substitute_type_ref_text(got, {});
    }
    return simple_literal_type(expr);
}

namespace {

std::string assignment_error_display(const std::string& expected, const Expr& expr,
                                     const TypeRef& got) {
    std::string message = "cannot assign " + assignment_type_display(expr, got) + " to " +
                          expected + " without an explicit cast";
    if (const std::optional<std::string> dyn_expr = dyn_shape_arithmetic_expr(got)) {
        message += "; shape expression uses runtime dyn in compile-time arithmetic: " + *dyn_expr;
    }
    return message;
}

std::string shape_dim_text(const TypeRef& dim) {
    return substitute_type_ref_text(dim, {});
}

std::string shape_list_text(const TypeRef& type) {
    std::string out = "[";
    if (type.kind == TypeKind::Shaped) {
        for (size_t i = 1; i < type.children.size(); ++i) {
            if (i > 1) {
                out += ", ";
            }
            out += shape_dim_text(type.children[i]);
        }
    }
    out += "]";
    return out;
}

bool shape_dim_accepts(const TypeRef& expected, const TypeRef& got) {
    return (expected.kind == TypeKind::Value && trim_copy(expected.value) == "dyn") ||
           type_ref_equivalent(expected, got);
}

std::optional<std::string> shape_assignment_detail(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != TypeKind::Shaped || got.kind != TypeKind::Shaped ||
        expected.children.empty() || got.children.empty() ||
        !type_assignment_allowed(expected.children.front(), got.children.front())) {
        return std::nullopt;
    }

    const size_t expected_rank = expected.children.size() - 1;
    const size_t got_rank = got.children.size() - 1;
    if (expected_rank != got_rank) {
        return "shape mismatch: expected rank " + std::to_string(expected_rank) + " " +
               shape_list_text(expected) + ", got rank " + std::to_string(got_rank) + " " +
               shape_list_text(got);
    }

    for (size_t axis = 0; axis < expected_rank; ++axis) {
        const TypeRef& expected_dim = expected.children[axis + 1];
        const TypeRef& got_dim = got.children[axis + 1];
        if (!shape_dim_accepts(expected_dim, got_dim)) {
            return "shape mismatch: expected " + shape_list_text(expected) + ", got " +
                   shape_list_text(got) + " (axis " + std::to_string(axis) + " expected " +
                   shape_dim_text(expected_dim) + ", got " + shape_dim_text(got_dim) + ")";
        }
    }

    return std::nullopt;
}

} // namespace

std::string assignment_error(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    std::string message =
        assignment_error_display(substitute_type_ref_text(expected, {}), expr, got);
    if (const std::optional<std::string> detail = shape_assignment_detail(expected, got)) {
        message += "; " + *detail;
    }
    return message;
}

} // namespace dudu
