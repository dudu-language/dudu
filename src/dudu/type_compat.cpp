#include "dudu/type_compat.hpp"

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

bool is_numeric_literal(std::string expr) {
    expr = trim_copy(std::move(expr));
    if (expr.empty() || std::isdigit(static_cast<unsigned char>(expr.front())) == 0) {
        return false;
    }
    for (const char c : expr) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '.' && c != '_') {
            return false;
        }
    }
    return true;
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

bool is_explicit_cast_to(const std::string& expected, std::string expr) {
    return starts_with(compact_type(std::move(expr)), compact_type(expected) + "(");
}

bool is_option_value(const std::string& expected, std::string expr, const std::string& got) {
    if (!starts_with(expected, "Option[") || expected.back() != ']') {
        return false;
    }
    expr = trim_copy(std::move(expr));
    const std::string inner = trim_copy(expected.substr(7, expected.size() - 8));
    return expr == "None" || got == inner ||
           (is_numeric_type(wrapped_type_arg(inner)) && is_numeric_literal(expr));
}

bool has_top_level_colon(const std::string& text) {
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (const char c : text) {
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == ':' && depth == 0) {
            return true;
        }
    }
    return false;
}

size_t top_level_colon(const std::string& text) {
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == ':' && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

std::string simple_literal_type(std::string expr) {
    expr = trim_copy(std::move(expr));
    if (expr == "True" || expr == "False") {
        return "bool";
    }
    if (expr == "None") {
        return "None";
    }
    if (starts_with(expr, "[") && ends_with(expr, "]")) {
        return "list";
    }
    if (starts_with(expr, "{") && ends_with(expr, "}")) {
        const std::vector<std::string> entries =
            split_top_level_args(expr.substr(1, expr.size() - 2));
        for (const std::string& entry : entries) {
            if (!entry.empty() && has_top_level_colon(entry)) {
                return "dict";
            }
        }
        return "set";
    }
    if (expr.size() >= 2 && ((expr.front() == '"' && expr.back() == '"') ||
                             (expr.front() == '\'' && expr.back() == '\''))) {
        return "str";
    }
    if (is_numeric_literal(expr)) {
        return "number";
    }
    return {};
}

bool literal_assignable_to(const std::string& expected, const std::string& expr) {
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

bool is_container_literal(const std::string& expected, std::string expr) {
    expr = trim_copy(std::move(expr));
    if (starts_with(expected, "list[")) {
        if (!starts_with(expr, "[") || !ends_with(expr, "]")) {
            return false;
        }
        const std::vector<std::string> args = type_args(expected, "list[");
        if (args.size() != 1) {
            return false;
        }
        const std::vector<std::string> entries =
            split_top_level_args(expr.substr(1, expr.size() - 2));
        for (const std::string& entry : entries) {
            if (!entry.empty() && !literal_assignable_to(args[0], entry)) {
                return false;
            }
        }
        return true;
    }
    if (starts_with(expected, "set[")) {
        if (!starts_with(expr, "{") || !ends_with(expr, "}")) {
            return false;
        }
        const std::vector<std::string> args = type_args(expected, "set[");
        if (args.size() != 1) {
            return false;
        }
        const std::vector<std::string> entries =
            split_top_level_args(expr.substr(1, expr.size() - 2));
        for (const std::string& entry : entries) {
            if (!entry.empty() &&
                (has_top_level_colon(entry) || !literal_assignable_to(args[0], entry))) {
                return false;
            }
        }
        return true;
    }
    if (!starts_with(expected, "dict[")) {
        return false;
    }
    if (!starts_with(expr, "{") || !ends_with(expr, "}")) {
        return false;
    }
    const std::vector<std::string> args = type_args(expected, "dict[");
    if (args.size() != 2) {
        return false;
    }
    const std::vector<std::string> entries = split_top_level_args(expr.substr(1, expr.size() - 2));
    for (const std::string& entry : entries) {
        if (entry.empty()) {
            continue;
        }
        const size_t colon = top_level_colon(entry);
        if (colon == std::string::npos) {
            return false;
        }
        if (!literal_assignable_to(args[0], entry.substr(0, colon)) ||
            !literal_assignable_to(args[1], entry.substr(colon + 1))) {
            return false;
        }
    }
    return true;
}

bool is_container_literal_expr(std::string expr) {
    expr = trim_copy(std::move(expr));
    return (starts_with(expr, "[") && ends_with(expr, "]")) ||
           (starts_with(expr, "{") && ends_with(expr, "}"));
}

bool is_reference_binding(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    if (expected.empty() || expected.front() != '&') {
        return false;
    }
    return wrapped_type_arg(trim_copy(expected.substr(1))) == got;
}

bool is_null_pointer(std::string expected, std::string expr, std::string got) {
    expected = trim_copy(std::move(expected));
    expr = trim_copy(std::move(expr));
    got = trim_copy(std::move(got));
    return !expected.empty() && expected.front() == '*' && expr == "None" && got == "None";
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

std::string wrapped_call_arg(std::string expr, std::string_view name) {
    expr = trim_copy(std::move(expr));
    const std::string prefix = std::string(name) + "(";
    if (!starts_with(expr, prefix) || !ends_with(expr, ")")) {
        return {};
    }
    const std::vector<std::string> args =
        split_top_level_args(expr.substr(prefix.size(), expr.size() - prefix.size() - 1));
    return args.size() == 1 ? args.front() : std::string{};
}

bool is_result_value(const std::string& expected, const std::string& expr, const std::string& got) {
    if (!starts_with(expected, "Result[") || expected.back() != ']') {
        return false;
    }
    const std::vector<std::string> parts =
        split_top_level_args(expected.substr(7, expected.size() - 8));
    if (parts.size() != 2) {
        return false;
    }
    if (starts_with(got, "Ok[") && got.back() == ']') {
        return assignment_type_allowed(parts[0], wrapped_call_arg(expr, "Ok"),
                                       got.substr(3, got.size() - 4));
    }
    if (starts_with(got, "Err[") && got.back() == ']') {
        return assignment_type_allowed(parts[1], wrapped_call_arg(expr, "Err"),
                                       got.substr(4, got.size() - 5));
    }
    return false;
}

} // namespace

bool assignment_type_allowed(const std::string& expected, const std::string& expr,
                             const std::string& got) {
    return expected == "auto" || is_explicit_cast_to(expected, expr) ||
           is_container_literal(expected, expr) ||
           (!is_container_literal_expr(expr) && got.empty()) || got == "auto" || got == expected ||
           compact_type(expected) == compact_type(got) || is_option_value(expected, expr, got) ||
           is_result_value(expected, expr, got) || is_null_pointer(expected, expr, got) ||
           is_reference_binding(expected, got) || is_function_type_match(expected, got) ||
           (is_numeric_type(wrapped_type_arg(expected)) && is_numeric_literal(expr));
}

} // namespace dudu
