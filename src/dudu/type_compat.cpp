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

} // namespace

bool assignment_type_allowed(const std::string& expected, const std::string& expr,
                             const std::string& got) {
    return is_explicit_cast_to(expected, expr) || got.empty() || got == "auto" || got == expected ||
           is_option_value(expected, expr, got) ||
           (is_numeric_type(wrapped_type_arg(expected)) && is_numeric_literal(expr));
}

} // namespace dudu
