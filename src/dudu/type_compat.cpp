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

bool is_container_literal(const std::string& expected, std::string expr) {
    expr = trim_copy(std::move(expr));
    if (!starts_with(expr, "{") || !ends_with(expr, "}")) {
        return false;
    }
    if (starts_with(expected, "set[")) {
        return true;
    }
    if (!starts_with(expected, "dict[")) {
        return false;
    }
    const std::vector<std::string> entries = split_top_level_args(expr.substr(1, expr.size() - 2));
    for (const std::string& entry : entries) {
        if (!entry.empty() && !has_top_level_colon(entry)) {
            return false;
        }
    }
    return true;
}

bool is_reference_binding(std::string expected, std::string got) {
    expected = trim_copy(std::move(expected));
    got = trim_copy(std::move(got));
    if (expected.empty() || expected.front() != '&') {
        return false;
    }
    return trim_copy(expected.substr(1)) == got;
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

} // namespace

bool assignment_type_allowed(const std::string& expected, const std::string& expr,
                             const std::string& got) {
    return is_explicit_cast_to(expected, expr) || got.empty() || got == "auto" || got == expected ||
           is_option_value(expected, expr, got) || is_container_literal(expected, expr) ||
           is_reference_binding(expected, got) || is_function_type_match(expected, got) ||
           (is_numeric_type(wrapped_type_arg(expected)) && is_numeric_literal(expr));
}

} // namespace dudu
