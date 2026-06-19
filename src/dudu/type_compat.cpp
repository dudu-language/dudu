#include "dudu/type_compat.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/type_compat_literals.hpp"
#include "dudu/type_compat_native.hpp"
#include "dudu/type_compat_structural.hpp"

#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace dudu {
namespace {

bool is_numeric_type_name(const std::string& type) {
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
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

std::string compact_type(std::string type) {
    std::string out;
    for (const char c : type) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            out.push_back(c);
        }
    }
    return out;
}

std::string normalize_c_tags(std::string type);

bool is_string_type(const TypeRef& type) {
    const std::string head = type_ref_head_name(normalize_cpp_type_artifacts_ref(type));
    return head == "str" || head == "std.string" || head == "std::string";
}

bool is_cstr_type(const TypeRef& type) {
    return type_ref_head_name(normalize_cpp_type_artifacts_ref(type)) == "cstr";
}

bool needs_rendered_type_fallback(const TypeRef& expected, const TypeRef& got) {
    return !has_type_ref(expected) || !has_type_ref(got);
}

bool rendered_type_fallback_allowed(const TypeRef& expected_ref, const TypeRef& got_ref) {
    if (!needs_rendered_type_fallback(expected_ref, got_ref)) {
        return false;
    }
    const std::string expected = substitute_type_ref_text(expected_ref, {});
    const std::string got = substitute_type_ref_text(got_ref, {});
    return got == expected || compact_type(expected) == compact_type(got) ||
           compact_type(normalize_c_tags(expected)) == compact_type(normalize_c_tags(got));
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
    if (expr.kind == ExprKind::TemplateCall && !expr.template_type_args.empty()) {
        TypeRef type;
        type.kind = TypeKind::Template;
        type.name = callee;
        type.children = expr.template_type_args;
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
    return expr.kind == ExprKind::IntLiteral || expr.kind == ExprKind::FloatLiteral;
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
        return type_ref_equivalent(wrapped_type_arg_ref(*target), got);
    }
    return false;
}

bool is_value_from_reference(const TypeRef& expected, const TypeRef& got) {
    if (const auto target = reference_target_ref(got)) {
        return type_ref_equivalent(expected, wrapped_type_arg_ref(*target));
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

bool is_null_pointer(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    return pointer_pointee_ref(expected).has_value() && expr.kind == ExprKind::NoneLiteral &&
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

std::string type_ref_spelling(const TypeRef& type) {
    return trim_copy(type_ref_head_name(type));
}

std::string type_ref_tail_name(const TypeRef& type) {
    std::string name = type_ref_spelling(type);
    const size_t dot = name.find_last_of(".:");
    if (dot != std::string::npos) {
        name = name.substr(dot + 1);
    }
    return name;
}

bool type_ref_ends_with_name(const TypeRef& type, const std::string& name) {
    const std::string spelling = type_ref_spelling(type);
    return spelling == name || spelling.ends_with("." + name) || spelling.ends_with("::" + name);
}

bool is_cpp_associated_type_binding(const TypeRef& expected, const TypeRef& got) {
    if (type_ref_equivalent(expected, got)) {
        return true;
    }
    static const std::set<std::string> associated = {
        "iterator", "const_iterator", "reference", "const_reference", "value_type",
        "pointer",  "const_pointer",  "size_type", "difference_type"};
    const std::string expected_name = type_ref_tail_name(expected);
    if (!associated.contains(expected_name)) {
        return false;
    }
    if ((expected_name == "size_type" || expected_name == "difference_type") &&
        is_numeric_type(got)) {
        return true;
    }
    if (type_ref_ends_with_name(got, expected_name)) {
        return true;
    }
    return expected_name == "const_iterator" && type_ref_ends_with_name(got, "iterator");
}

bool is_native_function_pointer(const TypeRef& expected, const TypeRef& got) {
    const auto expected_pointee = pointer_pointee_ref(expected);
    return expected_pointee && type_ref_is_void(wrapped_type_arg_ref(*expected_pointee)) &&
           got.kind == TypeKind::Function;
}

std::string normalize_c_tags(std::string type) {
    for (std::string_view tag : {"struct ", "class ", "union ", "enum "}) {
        size_t pos = type.find(tag);
        while (pos != std::string::npos) {
            type.erase(pos, tag.size());
            pos = type.find(tag, pos);
        }
    }
    return type;
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

bool has_internal_cpp_identifier(std::string_view name) {
    size_t pos = 0;
    while (pos < name.size()) {
        while (pos < name.size() &&
               (std::isalnum(static_cast<unsigned char>(name[pos])) == 0 && name[pos] != '_')) {
            ++pos;
        }
        const size_t start = pos;
        while (pos < name.size() &&
               (std::isalnum(static_cast<unsigned char>(name[pos])) != 0 || name[pos] == '_')) {
            ++pos;
        }
        if (pos > start && name.substr(start, 2) == "__") {
            return true;
        }
    }
    return false;
}

bool is_internal_cpp_template_artifact(const TypeRef& type) {
    if ((type.kind == TypeKind::Const || type.kind == TypeKind::Reference ||
         type.kind == TypeKind::Pointer) &&
        type.children.size() == 1) {
        return is_internal_cpp_template_artifact(type.children.front());
    }
    return type.kind == TypeKind::Template && !type.children.empty() &&
           has_internal_cpp_identifier(type.name);
}

bool is_native_internal_template_result(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != TypeKind::Template) {
        return false;
    }
    return is_internal_cpp_template_artifact(got);
}

bool is_value_from_const(const TypeRef& expected, const TypeRef& got) {
    const auto inner = unary_child_ref(got, TypeKind::Const);
    return inner.has_value() && type_ref_equivalent(expected, *inner);
}

bool normalized_type_assignment_allowed(const TypeRef& expected_ref, const TypeRef& got_ref) {
    if (has_type_ref(expected_ref) && has_type_ref(got_ref) &&
        structural_type_assignment_allowed(expected_ref, got_ref)) {
        return true;
    }
    return type_ref_is_auto(expected_ref) || !has_type_ref(got_ref) || type_ref_is_auto(got_ref) ||
           rendered_type_fallback_allowed(expected_ref, got_ref) ||
           (is_string_type(expected_ref) && is_string_type(got_ref)) ||
           is_void_pointer_target(expected_ref, got_ref) ||
           is_const_pointer_binding(expected_ref, got_ref) ||
           is_pointer_to_reference_value(expected_ref, got_ref) ||
           is_reference_binding(expected_ref, got_ref) ||
           is_value_from_reference(expected_ref, got_ref) ||
           is_value_from_const(expected_ref, got_ref) ||
           is_native_function_pointer(expected_ref, got_ref) ||
           is_native_internal_template_result(expected_ref, got_ref) ||
           is_cpp_associated_type_binding(expected_ref, got_ref);
}

} // namespace

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
           is_value_from_reference(expected, got) || is_value_from_const(expected, got) ||
           is_native_function_pointer(expected, got) ||
           is_cpp_associated_type_binding(expected, got) ||
           normalized_type_assignment_allowed(normalized_expected_ref, normalized_got_ref);
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
            is_value_from_reference(normalized_expected_ref, normalized_got_ref) ||
            is_value_from_const(normalized_expected_ref, normalized_got_ref) ||
            is_native_function_pointer(normalized_expected_ref, normalized_got_ref) ||
            is_cpp_associated_type_binding(normalized_expected_ref, normalized_got_ref)) {
            return true;
        }
    }
    return type_ref_is_auto(normalized_expected_ref) ||
           is_explicit_cast_to(normalized_expected_ref, expr) ||
           (!is_container_literal_expr(expr) && !has_type_ref(normalized_got_ref)) ||
           type_ref_is_auto(normalized_got_ref) ||
           rendered_type_fallback_allowed(normalized_expected_ref, normalized_got_ref) ||
           (is_string_type(normalized_expected_ref) && is_string_type(normalized_got_ref)) ||
           is_value_wrapper_assignment(normalized_expected_ref, expr, normalized_got_ref) ||
           is_null_pointer(normalized_expected_ref, expr, normalized_got_ref) ||
           is_void_pointer_target(normalized_expected_ref, normalized_got_ref) ||
           is_const_pointer_binding(normalized_expected_ref, normalized_got_ref) ||
           is_pointer_to_reference_value(normalized_expected_ref, normalized_got_ref) ||
           is_reference_binding(normalized_expected_ref, normalized_got_ref) ||
           is_value_from_reference(normalized_expected_ref, normalized_got_ref) ||
           is_value_from_const(normalized_expected_ref, normalized_got_ref) ||
           is_native_function_pointer(normalized_expected_ref, normalized_got_ref) ||
           is_native_internal_template_result(normalized_expected_ref, normalized_got_ref) ||
           is_cpp_associated_type_binding(normalized_expected_ref, normalized_got_ref) ||
           (is_cstr_type(normalized_expected_ref) && is_string_type(normalized_got_ref) &&
            expr.kind == ExprKind::StringLiteral) ||
           (is_numeric_type(normalized_expected_ref) && is_numeric_literal_expr(expr));
}

std::string display_type(const Expr& expr, const std::string& got) {
    return got.empty() ? simple_literal_type(expr) : got;
}

namespace {

std::string assignment_error_text(const std::string& expected, const Expr& expr,
                                  const std::string& got) {
    return "cannot assign " + display_type(expr, got) + " to " + expected +
           " without an explicit cast";
}

} // namespace

std::string assignment_error(const TypeRef& expected, const Expr& expr, const std::string& got) {
    return assignment_error_text(substitute_type_ref_text(expected, {}), expr, got);
}

std::string assignment_error(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    return assignment_error(expected, expr, substitute_type_ref_text(got, {}));
}

} // namespace dudu
