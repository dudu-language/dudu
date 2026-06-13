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

} // namespace

bool binary_rhs_allowed(const Symbols& symbols, const std::string& left,
                        const std::string& right_expr, const std::string& right) {
    const std::string resolved_left = resolve_alias(symbols, left);
    const std::string resolved_right = resolve_alias(symbols, right);
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    return assignment_type_allowed(left, right_expr, right) ||
           assignment_type_allowed(resolved_left, right_expr, resolved_right) ||
           assignment_type_allowed(value_left, right_expr, value_right) ||
           (!trim(resolved_left).empty() && trim(resolved_left).front() == '*' &&
            is_integer_type(value_right));
}

} // namespace dudu
