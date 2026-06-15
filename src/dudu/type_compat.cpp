#include "dudu/type_compat.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"

#include <cctype>
#include <set>

namespace dudu {
namespace {

bool is_numeric_type(const std::string& type) {
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

std::string wrapped_type_arg(std::string type) {
    type = trim_copy(std::move(type));
    const size_t open = type.find('[');
    if (open == std::string::npos || type.back() != ']') {
        return type;
    }
    const std::string wrapper = type.substr(0, open);
    if (wrapper != "const" && wrapper != "atomic" && wrapper != "volatile" && wrapper != "device" &&
        wrapper != "storage" && wrapper != "shared") {
        return type;
    }
    return trim_copy(type.substr(open + 1, type.size() - open - 2));
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
    if (!type.empty() && type.front() == '&') {
        return "&" + normalize_tuple_element(type.substr(1));
    }
    if (!starts_with(type, "__tuple_element_t[") || !type.ends_with("]")) {
        return type;
    }
    std::vector<std::string> args =
        split_top_level_args(type.substr(18, type.size() - 19));
    if (args.size() != 2) {
        return type;
    }
    args[0] = trim_copy(args[0]);
    args[1] = trim_copy(args[1]);
    if (args[0].empty() ||
        args[0].find_first_not_of("0123456789") != std::string::npos ||
        (!starts_with(args[1], "std.tuple[") && !starts_with(args[1], "tuple[")) ||
        !args[1].ends_with("]")) {
        return type;
    }
    const size_t open = args[1].find('[');
    const std::vector<std::string> elements =
        split_top_level_args(args[1].substr(open + 1, args[1].size() - open - 2));
    const size_t index = static_cast<size_t>(std::stoull(args[0]));
    if (index >= elements.size()) {
        return type;
    }
    return trim_copy(elements[index]);
}

std::string normalize_cpp_type_artifacts(std::string type) {
    return normalize_tuple_element(normalize_type_traits(std::move(type)));
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

std::vector<std::string> type_args(const std::string& type, std::string_view prefix) {
    const std::string text = trim_copy(type);
    if (!starts_with(text, prefix) || !ends_with(text, "]")) {
        return {};
    }
    return split_top_level_args(text.substr(prefix.size(), text.size() - prefix.size() - 1));
}

bool is_container_literal(const std::string& expected, const Expr& expr) {
    if (starts_with(expected, "list[")) {
        if (expr.kind != ExprKind::ListLiteral) {
            return false;
        }
        const std::vector<std::string> args = type_args(expected, "list[");
        if (args.size() != 1) {
            return false;
        }
        for (const Expr& entry : expr.children) {
            if (!literal_assignable_to(args[0], entry)) {
                return false;
            }
        }
        return true;
    }
    if (starts_with(expected, "set[")) {
        if (expr.kind != ExprKind::SetLiteral) {
            return false;
        }
        const std::vector<std::string> args = type_args(expected, "set[");
        if (args.size() != 1) {
            return false;
        }
        for (const Expr& entry : expr.children) {
            if (!literal_assignable_to(args[0], entry)) {
                return false;
            }
        }
        return true;
    }
    if (!starts_with(expected, "dict[")) {
        return false;
    }
    if (expr.kind == ExprKind::SetLiteral && expr.children.empty()) {
        return true;
    }
    if (expr.kind != ExprKind::DictLiteral) {
        return false;
    }
    const std::vector<std::string> args = type_args(expected, "dict[");
    if (args.size() != 2) {
        return false;
    }
    for (const Expr& entry : expr.children) {
        if (entry.children.size() != 2 || !literal_assignable_to(args[0], entry.children[0]) ||
            !literal_assignable_to(args[1], entry.children[1])) {
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
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    if (expected.empty() || expected.front() != '&') {
        return false;
    }
    return wrapped_type_arg(trim_copy(expected.substr(1))) == got;
}

bool is_value_from_reference(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    if (got.empty() || got.front() != '&') {
        return false;
    }
    return expected == wrapped_type_arg(trim_copy(got.substr(1)));
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
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    return !expected.empty() && expected.front() == '*' && expr.kind == ExprKind::NoneLiteral &&
           got == "None";
}

bool is_void_pointer_target(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    return !expected.empty() && !got.empty() && expected.front() == '*' && got.front() == '*' &&
           (wrapped_type_arg(expected.substr(1)) == "void" ||
            wrapped_type_arg(expected.substr(1)) == "const[void]");
}

bool is_const_pointer_binding(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    if (expected.size() < 9 || got.size() < 2 || expected.front() != '*' || got.front() != '*' ||
        !starts_with(expected.substr(1), "const[") || expected.back() != ']') {
        return false;
    }
    const std::string inner = expected.substr(7, expected.size() - 8);
    return wrapped_type_arg(got.substr(1)) == inner;
}

bool is_pointer_to_reference_value(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    return expected.size() > 1 && got.size() > 2 && expected.front() == '*' &&
           got.rfind("*&", 0) == 0 &&
           wrapped_type_arg(expected.substr(1)) == wrapped_type_arg(got.substr(2));
}

bool is_cpp_associated_type_binding(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    if (expected == got) {
        return true;
    }
    static const std::set<std::string> associated = {
        "iterator",       "const_iterator", "reference",     "const_reference",
        "value_type",     "pointer",        "const_pointer", "size_type",
        "difference_type"};
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
    if (!starts_with(type, "fn(")) {
        return type;
    }
    const size_t close = type.find(')');
    if (close == std::string::npos) {
        return type;
    }
    if (type.find("->", close) != std::string::npos) {
        return type;
    }
    return type + " -> void";
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
    if (!starts_with(expected, "Result[") || expected.back() != ']') {
        return false;
    }
    const std::vector<std::string> parts =
        split_top_level_args(expected.substr(7, expected.size() - 8));
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
    if (!starts_with(expected, "Option[") || expected.back() != ']') {
        return false;
    }
    const std::string inner = trim_copy(expected.substr(7, expected.size() - 8));
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

std::string display_type(const Expr& expr, const std::string& got) {
    return got.empty() ? simple_literal_type(expr) : got;
}

std::string assignment_error(const std::string& expected, const Expr& expr,
                             const std::string& got) {
    return "cannot assign " + display_type(expr, got) + " to " + expected +
           " without an explicit cast";
}

} // namespace dudu
