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

bool is_numeric_type(const std::string& type) {
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

std::string wrapped_type_arg(std::string type) {
    type = trim_copy(std::move(type));
    if (const auto inner =
            unary_type_child_text(type, {TypeKind::Const, TypeKind::Atomic, TypeKind::Volatile,
                                         TypeKind::Device, TypeKind::Storage, TypeKind::Shared})) {
        return *inner;
    }
    return type;
}

std::optional<std::string> unary_child(std::string type, TypeKind kind) {
    type = trim_copy(std::move(type));
    return unary_type_child_text(parse_type_text(type), kind);
}

std::optional<std::string> pointer_pointee(std::string type) {
    return unary_child(std::move(type), TypeKind::Pointer);
}

std::optional<std::string> reference_target(std::string type) {
    return unary_child(std::move(type), TypeKind::Reference);
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

bool is_string_type(const std::string& type) {
    const std::string compact = compact_type(normalize_cpp_type_artifacts(type));
    return compact == "str" || compact == "std.string" || compact == "std::string";
}

bool is_explicit_cast_to(const std::string& expected, const Expr& expr) {
    if (expr.kind != ExprKind::Call && expr.kind != ExprKind::TemplateCall) {
        return false;
    }
    const std::string callee = direct_callee_name(expr);
    return compact_type(callee.empty() ? call_callee_text(expr) : callee) == compact_type(expected);
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

bool is_reference_binding(std::string expected, std::string got) {
    got = trim_copy(std::move(got));
    if (const auto target = reference_target(std::move(expected))) {
        return wrapped_type_arg(*target) == got;
    }
    return false;
}

bool is_value_from_reference(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    if (const auto target = reference_target(std::move(got))) {
        return expected == wrapped_type_arg(*target);
    }
    return false;
}

bool is_value_wrapper_assignment(std::string expected, const Expr& expr, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    const std::string inner = wrapped_type_arg(expected);
    if (inner == expected) {
        return false;
    }
    return assignment_type_allowed(inner, expr, got);
}

bool is_null_pointer(std::string expected, const Expr& expr, std::string got) {
    got = trim_copy(std::move(got));
    return pointer_pointee(std::move(expected)).has_value() && expr.kind == ExprKind::NoneLiteral &&
           got == "None";
}

bool is_void_pointer_target(std::string expected, std::string got) {
    const auto expected_pointee = pointer_pointee(std::move(expected));
    const auto got_pointee = pointer_pointee(std::move(got));
    if (!expected_pointee || !got_pointee) {
        return false;
    }
    return wrapped_type_arg(*expected_pointee) == "void" ||
           wrapped_type_arg(*expected_pointee) == "const[void]";
}

bool is_const_pointer_binding(std::string expected, std::string got) {
    const auto expected_pointee = pointer_pointee(std::move(expected));
    const auto got_pointee = pointer_pointee(std::move(got));
    if (!expected_pointee || !got_pointee) {
        return false;
    }
    const auto expected_const_inner = unary_child(*expected_pointee, TypeKind::Const);
    return expected_const_inner && wrapped_type_arg(*got_pointee) == *expected_const_inner;
}

bool is_pointer_to_reference_value(std::string expected, std::string got) {
    const auto expected_pointee = pointer_pointee(std::move(expected));
    const auto got_pointee = pointer_pointee(std::move(got));
    if (!expected_pointee || !got_pointee) {
        return false;
    }
    const auto got_reference_target = reference_target(*got_pointee);
    return got_reference_target &&
           wrapped_type_arg(*expected_pointee) == wrapped_type_arg(*got_reference_target);
}

bool is_cpp_associated_type_binding(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    if (expected == got) {
        return true;
    }
    static const std::set<std::string> associated = {
        "iterator", "const_iterator", "reference", "const_reference", "value_type",
        "pointer",  "const_pointer",  "size_type", "difference_type"};
    std::string expected_name = expected;
    const size_t dot = expected_name.find_last_of(".:");
    if (dot != std::string::npos) {
        expected_name = expected_name.substr(dot + 1);
    }
    if (!associated.contains(expected_name)) {
        return false;
    }
    if ((expected_name == "size_type" || expected_name == "difference_type") &&
        is_numeric_type(wrapped_type_arg(got))) {
        return true;
    }
    if (got == expected || got.ends_with("." + expected_name) ||
        got.ends_with("::" + expected_name)) {
        return true;
    }
    return expected_name == "const_iterator" && (got == "iterator" || got.ends_with(".iterator"));
}

std::string normalize_function_type(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    return parsed.kind == TypeKind::Function ? substitute_type_ref_text(parsed, {}) : type;
}

bool is_function_type_match(std::string expected, std::string got) {
    expected = normalize_function_type(std::move(expected));
    got = normalize_function_type(std::move(got));
    return starts_with(expected, "fn(") && expected == got;
}

bool is_native_function_pointer(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    return expected == "*void" && starts_with(normalize_function_type(std::move(got)), "fn(");
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

bool is_variant_value(const TypeRef& expected, const Expr& expr, const std::string& got) {
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

bool is_native_internal_template_result(std::string expected, std::string got) {
    const TypeRef expected_ref = parse_type_text(std::move(expected));
    if (expected_ref.kind != TypeKind::Template) {
        return false;
    }
    return is_internal_cpp_template_artifact(parse_type_text(std::move(got)));
}

bool is_value_from_const(std::string expected, std::string got) {
    const auto inner = unary_child(std::move(got), TypeKind::Const);
    return inner.has_value() && compact_type(expected) == compact_type(*inner);
}

} // namespace

bool type_assignment_allowed(const std::string& expected, const std::string& got) {
    const std::string normalized_expected = normalize_cpp_type_artifacts(expected);
    const std::string normalized_got = normalize_cpp_type_artifacts(got);
    if (!normalized_expected.empty() && !normalized_got.empty() &&
        structural_type_assignment_allowed(parse_type_text(normalized_expected),
                                           parse_type_text(normalized_got))) {
        return true;
    }
    return normalized_expected == "auto" || normalized_got.empty() || normalized_got == "auto" ||
           normalized_got == normalized_expected ||
           compact_type(normalized_expected) == compact_type(normalized_got) ||
           compact_type(normalize_c_tags(normalized_expected)) ==
               compact_type(normalize_c_tags(normalized_got)) ||
           (is_string_type(normalized_expected) && is_string_type(normalized_got)) ||
           is_void_pointer_target(normalized_expected, normalized_got) ||
           is_const_pointer_binding(normalized_expected, normalized_got) ||
           is_pointer_to_reference_value(normalized_expected, normalized_got) ||
           is_reference_binding(normalized_expected, normalized_got) ||
           is_value_from_reference(normalized_expected, normalized_got) ||
           is_value_from_const(normalized_expected, normalized_got) ||
           is_function_type_match(normalized_expected, normalized_got) ||
           is_native_function_pointer(normalized_expected, normalized_got) ||
           is_native_internal_template_result(normalized_expected, normalized_got) ||
           is_cpp_associated_type_binding(normalized_expected, normalized_got);
}

bool type_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    return structural_type_assignment_allowed(expected, got) ||
           type_assignment_allowed(substitute_type_ref_text(expected, {}),
                                   substitute_type_ref_text(got, {}));
}

bool assignment_type_allowed(const std::string& expected, const Expr& expr,
                             const std::string& got) {
    const std::string normalized_expected = normalize_cpp_type_artifacts(expected);
    const std::string normalized_got = normalize_cpp_type_artifacts(got);
    const TypeRef expected_ref = parse_type_text(normalized_expected);
    if (!normalized_got.empty() && normalized_got != "auto" &&
        structural_type_assignment_allowed(expected_ref, parse_type_text(normalized_got))) {
        return true;
    }
    return normalized_expected == "auto" || is_explicit_cast_to(normalized_expected, expr) ||
           parsed_expected_literal_assignment_allowed(expected_ref, expr, normalized_got) ||
           (!is_container_literal_expr(expr) && normalized_got.empty()) ||
           normalized_got == "auto" || normalized_got == normalized_expected ||
           compact_type(normalized_expected) == compact_type(normalized_got) ||
           is_variant_value(expected_ref, expr, normalized_got) ||
           compact_type(normalize_c_tags(normalized_expected)) ==
               compact_type(normalize_c_tags(normalized_got)) ||
           (is_string_type(normalized_expected) && is_string_type(normalized_got)) ||
           is_value_wrapper_assignment(normalized_expected, expr, normalized_got) ||
           is_null_pointer(normalized_expected, expr, normalized_got) ||
           is_void_pointer_target(normalized_expected, normalized_got) ||
           is_const_pointer_binding(normalized_expected, normalized_got) ||
           is_pointer_to_reference_value(normalized_expected, normalized_got) ||
           is_reference_binding(normalized_expected, normalized_got) ||
           is_value_from_reference(normalized_expected, normalized_got) ||
           is_value_from_const(normalized_expected, normalized_got) ||
           is_function_type_match(normalized_expected, normalized_got) ||
           is_native_function_pointer(normalized_expected, normalized_got) ||
           is_native_internal_template_result(normalized_expected, normalized_got) ||
           is_cpp_associated_type_binding(normalized_expected, normalized_got) ||
           (normalized_expected == "cstr" && normalized_got == "str" &&
            expr.kind == ExprKind::StringLiteral) ||
           (is_numeric_type(wrapped_type_arg(normalized_expected)) &&
            simple_literal_type(expr) == "number");
}

bool assignment_type_allowed(const TypeRef& expected, const Expr& expr, const std::string& got) {
    const std::string expected_text = substitute_type_ref_text(expected, {});
    const std::string normalized_expected = normalize_cpp_type_artifacts(expected_text);
    const std::string normalized_got = normalize_cpp_type_artifacts(got);
    if (normalized_expected != expected_text) {
        return assignment_type_allowed(normalized_expected, expr, normalized_got);
    }
    if (!normalized_got.empty() && normalized_got != "auto") {
        const TypeRef got_ref = parse_type_text(normalized_got);
        if (structural_type_assignment_allowed(expected, got_ref)) {
            return true;
        }
    }
    if (parsed_expected_literal_assignment_allowed(expected, expr, normalized_got)) {
        return true;
    }
    if (is_variant_value(expected, expr, normalized_got)) {
        return true;
    }
    return normalized_expected == "auto" || is_explicit_cast_to(normalized_expected, expr) ||
           (!is_container_literal_expr(expr) && normalized_got.empty()) ||
           normalized_got == "auto" || normalized_got == normalized_expected ||
           compact_type(normalized_expected) == compact_type(normalized_got) ||
           compact_type(normalize_c_tags(normalized_expected)) ==
               compact_type(normalize_c_tags(normalized_got)) ||
           (is_string_type(normalized_expected) && is_string_type(normalized_got)) ||
           is_value_wrapper_assignment(normalized_expected, expr, normalized_got) ||
           is_null_pointer(normalized_expected, expr, normalized_got) ||
           is_void_pointer_target(normalized_expected, normalized_got) ||
           is_const_pointer_binding(normalized_expected, normalized_got) ||
           is_pointer_to_reference_value(normalized_expected, normalized_got) ||
           is_reference_binding(normalized_expected, normalized_got) ||
           is_value_from_reference(normalized_expected, normalized_got) ||
           is_value_from_const(normalized_expected, normalized_got) ||
           is_function_type_match(normalized_expected, normalized_got) ||
           is_native_function_pointer(normalized_expected, normalized_got) ||
           is_native_internal_template_result(normalized_expected, normalized_got) ||
           is_cpp_associated_type_binding(normalized_expected, normalized_got) ||
           (normalized_expected == "cstr" && normalized_got == "str" &&
            expr.kind == ExprKind::StringLiteral) ||
           (is_numeric_type(wrapped_type_arg(normalized_expected)) &&
            simple_literal_type(expr) == "number");
}

std::string display_type(const Expr& expr, const std::string& got) {
    return got.empty() ? simple_literal_type(expr) : got;
}

std::string assignment_error(const std::string& expected, const Expr& expr,
                             const std::string& got) {
    return "cannot assign " + display_type(expr, got) + " to " + expected +
           " without an explicit cast";
}

std::string assignment_error(const TypeRef& expected, const Expr& expr, const std::string& got) {
    return assignment_error(substitute_type_ref_text(expected, {}), expr, got);
}

} // namespace dudu
