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
    return expected == "auto" || got.empty() || got == "auto" || got == expected ||
           compact_type(expected) == compact_type(got) ||
           compact_type(normalize_c_tags(expected)) == compact_type(normalize_c_tags(got)) ||
           is_void_pointer_target(expected, got) || is_const_pointer_binding(expected, got) ||
           is_pointer_to_reference_value(expected, got) || is_reference_binding(expected, got) ||
           is_value_from_reference(expected, got) || is_function_type_match(expected, got) ||
           is_native_function_pointer(expected, got);
}

bool assignment_type_allowed(const std::string& expected, const Expr& expr,
                             const std::string& got) {
    return expected == "auto" || is_explicit_cast_to(expected, expr) ||
           is_container_literal(expected, expr) ||
           (!is_container_literal_expr(expr) && got.empty()) || got == "auto" || got == expected ||
           compact_type(expected) == compact_type(got) || is_option_value(expected, expr, got) ||
           compact_type(normalize_c_tags(expected)) == compact_type(normalize_c_tags(got)) ||
           is_result_value(expected, expr, got) ||
           is_value_wrapper_assignment(expected, expr, got) ||
           is_null_pointer(expected, expr, got) || is_void_pointer_target(expected, got) ||
           is_const_pointer_binding(expected, got) ||
           is_pointer_to_reference_value(expected, got) || is_reference_binding(expected, got) ||
           is_value_from_reference(expected, got) || is_function_type_match(expected, got) ||
           is_native_function_pointer(expected, got) ||
           (expected == "cstr" && got == "str" && expr.kind == ExprKind::StringLiteral) ||
           (is_numeric_type(wrapped_type_arg(expected)) && simple_literal_type(expr) == "number");
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
