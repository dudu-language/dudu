#include "dudu/type_compat.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/type_compat_literals.hpp"

#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string_view>

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

size_t matching_angle(const std::string& text, const size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '<') {
            ++depth;
        } else if (text[i] == '>') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::string normalize_type_traits(std::string type) {
    type = trim_copy(std::move(type));
    const std::string prefix = "typename __decay_and_strip<";
    size_t pos = type.find(prefix);
    while (pos != std::string::npos) {
        const size_t open = pos + prefix.size() - 1;
        const size_t close = matching_angle(type, open);
        if (close == std::string::npos) {
            return type;
        }
        size_t suffix_end = close + 1;
        std::string suffix;
        if (type.compare(suffix_end, 7, ".__type") == 0) {
            suffix = ".__type";
        } else if (type.compare(suffix_end, 8, "::__type") == 0) {
            suffix = "::__type";
        } else {
            pos = type.find(prefix, close + 1);
            continue;
        }
        suffix_end += suffix.size();
        const std::string inner = trim_copy(type.substr(pos + prefix.size(), close - open - 1));
        type.replace(pos, suffix_end - pos, inner);
        pos = type.find(prefix, pos + inner.size());
    }
    return trim_copy(type);
}

std::string normalize_tuple_element(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind == TypeKind::Reference && parsed.children.size() == 1) {
        return "&" + normalize_tuple_element(parsed.children.front().text);
    }
    if (parsed.kind != TypeKind::Template || parsed.name != "__tuple_element_t" ||
        parsed.children.size() != 2 || parsed.children[0].kind != TypeKind::Value) {
        return type;
    }
    const TypeRef& tuple_type = parsed.children[1];
    if (tuple_type.kind != TypeKind::Template ||
        (tuple_type.name != "std.tuple" && tuple_type.name != "tuple")) {
        return type;
    }
    const std::string index_text = trim_copy(parsed.children[0].value);
    if (index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) {
        return type;
    }
    const size_t index = static_cast<size_t>(std::stoull(index_text));
    if (index >= tuple_type.children.size()) {
        return type;
    }
    return trim_copy(tuple_type.children[index].text);
}

std::string normalize_cpp_type_artifacts(std::string type) {
    type = normalize_tuple_element(normalize_type_traits(std::move(type)));
    for (const std::string_view marker : {"* const[", "& const[", "* volatile[", "& volatile["}) {
        size_t pos = type.find(marker);
        while (pos != std::string::npos) {
            type.erase(pos + 1, 1);
            pos = type.find(marker, pos + 1);
        }
    }
    return type;
}

bool is_string_type(const std::string& type) {
    const std::string compact = compact_type(normalize_cpp_type_artifacts(type));
    return compact == "str" || compact == "std.string" || compact == "std::string";
}

bool is_explicit_cast_to(const std::string& expected, const Expr& expr) {
    return (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
           compact_type(call_callee_text(expr)) == compact_type(expected);
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

bool literal_assignable_to(const std::string& expected, const Expr& expr) {
    const std::string got = simple_literal_type(expr);
    return got == "number" ? is_numeric_type(wrapped_type_arg(expected))
                           : assignment_type_allowed(expected, expr, got);
}

bool is_container_literal(const std::string& expected, const Expr& expr) {
    const std::vector<std::string> list_args = template_type_arg_texts(expected, "list");
    if (!list_args.empty()) {
        if (expr.kind != ExprKind::ListLiteral) {
            return false;
        }
        if (list_args.size() != 1) {
            return false;
        }
        for (const Expr& entry : expr.children) {
            if (!literal_assignable_to(list_args[0], entry)) {
                return false;
            }
        }
        return true;
    }
    const std::vector<std::string> set_args = template_type_arg_texts(expected, "set");
    if (!set_args.empty()) {
        if (expr.kind != ExprKind::SetLiteral) {
            return false;
        }
        if (set_args.size() != 1) {
            return false;
        }
        for (const Expr& entry : expr.children) {
            if (!literal_assignable_to(set_args[0], entry)) {
                return false;
            }
        }
        return true;
    }
    const std::vector<std::string> dict_args = template_type_arg_texts(expected, "dict");
    if (dict_args.empty()) {
        return false;
    }
    if (expr.kind == ExprKind::SetLiteral && expr.children.empty()) {
        return true;
    }
    if (expr.kind != ExprKind::DictLiteral) {
        return false;
    }
    if (dict_args.size() != 2) {
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
    if (!associated.contains(expected)) {
        return false;
    }
    if (got == expected || got.ends_with("." + expected)) {
        return true;
    }
    return expected == "const_iterator" && (got == "iterator" || got.ends_with(".iterator"));
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

bool is_result_value(const std::string& expected, const Expr& expr, const std::string& got) {
    const std::vector<std::string> parts = template_type_arg_texts(expected, "Result");
    if (parts.size() != 2 || expr.kind != ExprKind::Call || expr.children.size() != 1) {
        return false;
    }
    const std::string callee = call_callee_text(expr);
    if (starts_with(got, "Ok[") && got.back() == ']' && callee == "Ok") {
        return assignment_type_allowed(parts[0], expr.children.front(),
                                       got.substr(3, got.size() - 4));
    }
    if (starts_with(got, "Err[") && got.back() == ']' && callee == "Err") {
        return assignment_type_allowed(parts[1], expr.children.front(),
                                       got.substr(4, got.size() - 5));
    }
    return false;
}

bool is_option_value(const std::string& expected, const Expr& expr, const std::string& got) {
    const std::vector<std::string> parts = template_type_arg_texts(expected, "Option");
    if (parts.size() != 1) {
        return false;
    }
    const std::string& inner = parts[0];
    return expr.kind == ExprKind::NoneLiteral || got == inner ||
           (is_numeric_type(wrapped_type_arg(inner)) && simple_literal_type(expr) == "number");
}

} // namespace

bool type_assignment_allowed(const std::string& expected, const std::string& got) {
    const std::string normalized_expected = normalize_cpp_type_artifacts(expected);
    const std::string normalized_got = normalize_cpp_type_artifacts(got);
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
           is_function_type_match(normalized_expected, normalized_got) ||
           is_native_function_pointer(normalized_expected, normalized_got) ||
           is_cpp_associated_type_binding(normalized_expected, normalized_got);
}

bool type_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    return type_assignment_allowed(substitute_type_ref_text(expected, {}),
                                   substitute_type_ref_text(got, {}));
}

bool assignment_type_allowed(const std::string& expected, const Expr& expr,
                             const std::string& got) {
    const std::string normalized_expected = normalize_cpp_type_artifacts(expected);
    const std::string normalized_got = normalize_cpp_type_artifacts(got);
    return normalized_expected == "auto" || is_explicit_cast_to(normalized_expected, expr) ||
           is_container_literal(normalized_expected, expr) ||
           (!is_container_literal_expr(expr) && normalized_got.empty()) ||
           normalized_got == "auto" || normalized_got == normalized_expected ||
           compact_type(normalized_expected) == compact_type(normalized_got) ||
           is_option_value(normalized_expected, expr, normalized_got) ||
           compact_type(normalize_c_tags(normalized_expected)) ==
               compact_type(normalize_c_tags(normalized_got)) ||
           (is_string_type(normalized_expected) && is_string_type(normalized_got)) ||
           is_result_value(normalized_expected, expr, normalized_got) ||
           is_value_wrapper_assignment(normalized_expected, expr, normalized_got) ||
           is_null_pointer(normalized_expected, expr, normalized_got) ||
           is_void_pointer_target(normalized_expected, normalized_got) ||
           is_const_pointer_binding(normalized_expected, normalized_got) ||
           is_pointer_to_reference_value(normalized_expected, normalized_got) ||
           is_reference_binding(normalized_expected, normalized_got) ||
           is_value_from_reference(normalized_expected, normalized_got) ||
           is_function_type_match(normalized_expected, normalized_got) ||
           is_native_function_pointer(normalized_expected, normalized_got) ||
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
    if (parsed_expected_literal_assignment_allowed(expected, expr, normalized_got)) {
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
           is_function_type_match(normalized_expected, normalized_got) ||
           is_native_function_pointer(normalized_expected, normalized_got) ||
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
