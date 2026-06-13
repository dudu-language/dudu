#include "dudu/sema_ops.hpp"

#include "dudu/type_compat.hpp"

#include <set>

namespace dudu {
namespace {

bool is_integer_type(std::string type) {
    type = trim(std::move(type));
    static const std::set<std::string> integers = {"i8",  "i16", "i32", "i64",   "u8",
                                                   "u16", "u32", "u64", "usize", "isize"};
    return integers.contains(type);
}

bool is_numeric_type(std::string type) {
    type = trim(std::move(type));
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

std::string unwrap_value_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        bool unwrapped = false;
        for (const char* wrapper : {"const", "volatile", "storage", "shared", "device"}) {
            const std::string prefix = std::string(wrapper) + "[";
            if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
                type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
                unwrapped = true;
                break;
            }
        }
        if (!unwrapped) {
            return type;
        }
    }
}

bool unknown_or_auto(const std::string& type) {
    const std::string text = trim(type);
    return text.empty() || text == "auto";
}

bool same_foreign_cpp_type(const std::string& left, const std::string& right) {
    const std::string lhs = trim(left);
    return !lhs.empty() && lhs == trim(right) && lhs.find('.') != std::string::npos;
}

bool numeric_operand_allowed(const std::string& expected, const std::string& expr,
                             const std::string& got) {
    return is_numeric_type(expected) && assignment_type_allowed(expected, expr, got);
}

bool same_or_assignable(const std::string& left, const std::string& right_expr,
                        const std::string& right) {
    return assignment_type_allowed(left, right_expr, right) ||
           assignment_type_allowed(right, "", left);
}

} // namespace

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                        const std::string& right_expr, const std::string& right) {
    const std::string resolved_left = resolve_alias(symbols, left);
    const std::string resolved_right = resolve_alias(symbols, right);
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    if (unknown_or_auto(value_left) || unknown_or_auto(value_right)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left, value_right)) {
        return true;
    }
    if (op == "+" && value_left == "str") {
        return assignment_type_allowed(value_left, right_expr, value_right);
    }
    if (op == "+" || op == "-") {
        return (!trim(resolved_left).empty() && trim(resolved_left).front() == '*' &&
                is_integer_type(value_right)) ||
               numeric_operand_allowed(value_left, right_expr, value_right);
    }
    if (op == "*" || op == "/") {
        return numeric_operand_allowed(value_left, right_expr, value_right);
    }
    if (op == "%" || op == "^" || op == "&" || op == "|" || op == "<<" || op == ">>") {
        return is_integer_type(value_left) &&
               (assignment_type_allowed(value_left, right_expr, value_right) ||
                is_integer_type(value_right));
    }
    return false;
}

bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                            const std::string& right_expr, const std::string& right) {
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    if (unknown_or_auto(value_left) || unknown_or_auto(value_right)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left, value_right)) {
        return true;
    }
    if (op == "==" || op == "!=") {
        return same_or_assignable(value_left, right_expr, value_right);
    }
    if (value_left == "str") {
        return assignment_type_allowed(value_left, right_expr, value_right);
    }
    return numeric_operand_allowed(value_left, right_expr, value_right);
}

} // namespace dudu
