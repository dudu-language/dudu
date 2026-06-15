#include "dudu/cpp_lower.hpp"

#include <algorithm>
#include <cctype>

namespace dudu {
std::string lower_raw_cpp_escape_expr(std::string expr) {
    return lower_raw_cpp_escape_expr(expr, {});
}

std::string lower_generic_type_constructor(std::string expr) {
    size_t open = expr.find('[');
    while (open != std::string::npos) {
        size_t name_start = open;
        while (name_start > 0) {
            const char c = expr[name_start - 1];
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
                break;
            }
            --name_start;
        }
        if (name_start == open) {
            open = expr.find('[', open + 1);
            continue;
        }
        size_t prefix = name_start;
        while (prefix > 0 && std::isspace(static_cast<unsigned char>(expr[prefix - 1])) != 0) {
            --prefix;
        }
        if (prefix > 0 && expr[prefix - 1] == '.') {
            open = expr.find('[', open + 1);
            continue;
        }
        if (prefix > 0 && expr[prefix - 1] == '*') {
            open = expr.find('[', open + 1);
            continue;
        }
        const std::string name = expr.substr(name_start, open - name_start);
        if (name == "new" || name == "malloc" || name == "sizeof" || name == "alignof" ||
            name == "offsetof") {
            open = expr.find('[', open + 1);
            continue;
        }
        const size_t close = expr.find(']', open + 1);
        if (close == std::string::npos || close + 1 >= expr.size() || expr[close + 1] != '(') {
            open = expr.find('[', open + 1);
            continue;
        }
        int depth = 1;
        size_t cursor = close + 2;
        while (cursor < expr.size() && depth > 0) {
            if (expr[cursor] == '(') {
                ++depth;
            } else if (expr[cursor] == ')') {
                --depth;
            }
            ++cursor;
        }
        if (depth != 0) {
            break;
        }
        const std::string type = expr.substr(name_start, close - name_start + 1);
        const std::string args = expr.substr(close + 2, cursor - close - 3);
        const std::string replacement =
            lower_cpp_type(type) + "(" + lower_raw_cpp_escape_expr(args) + ")";
        expr.replace(name_start, cursor - name_start, replacement);
        open = expr.find('[', name_start + replacement.size());
    }
    return expr;
}

std::string lower_len_calls(std::string expr) {
    const std::string marker = "len(";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 && expr[pos - 1] != '_');
        if (!left_ok) {
            pos = expr.find(marker, pos + marker.size());
            continue;
        }
        int depth = 1;
        size_t cursor = pos + marker.size();
        while (cursor < expr.size() && depth > 0) {
            if (expr[cursor] == '(') {
                ++depth;
            } else if (expr[cursor] == ')') {
                --depth;
            }
            ++cursor;
        }
        if (depth != 0) {
            break;
        }
        const std::string arg = expr.substr(pos + marker.size(), cursor - pos - marker.size() - 1);
        const std::string replacement = "(" + lower_raw_cpp_escape_expr(arg) + ").size()";
        expr.replace(pos, cursor - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
}

std::string lower_numeric_separators(std::string expr) {
    for (size_t i = 1; i + 1 < expr.size();) {
        const bool numeric_separator = expr[i] == '_' &&
                                       std::isdigit(static_cast<unsigned char>(expr[i - 1])) != 0 &&
                                       std::isdigit(static_cast<unsigned char>(expr[i + 1])) != 0;
        const bool in_identifier =
            numeric_separator &&
            ((i > 1 &&
              (std::isalpha(static_cast<unsigned char>(expr[i - 2])) != 0 || expr[i - 2] == '_')) ||
             (i + 2 < expr.size() &&
              (std::isalpha(static_cast<unsigned char>(expr[i + 2])) != 0 || expr[i + 2] == '_')));
        if (numeric_separator && !in_identifier) {
            expr.erase(i, 1);
            continue;
        }
        ++i;
    }
    return expr;
}

std::string lower_pointer_cast_calls(std::string expr) {
    static const std::vector<std::string> builtin_pointer_types = {
        "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64", "str", "cstr"};
    size_t pos = expr.find('*');
    while (pos != std::string::npos) {
        size_t prefix = pos;
        while (prefix > 0 && std::isspace(static_cast<unsigned char>(expr[prefix - 1])) != 0) {
            --prefix;
        }
        const bool follows_value =
            prefix > 0 && (std::isalnum(static_cast<unsigned char>(expr[prefix - 1])) != 0 ||
                           expr[prefix - 1] == ')' || expr[prefix - 1] == ']');
        const bool left_ok =
            !follows_value &&
            (pos == 0 || (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 &&
                          expr[pos - 1] != '_'));
        size_t name_start = pos + 1;
        while (name_start < expr.size() &&
               std::isspace(static_cast<unsigned char>(expr[name_start])) != 0) {
            ++name_start;
        }
        size_t name_end = name_start;
        int bracket_depth = 0;
        while (name_end < expr.size()) {
            const char c = expr[name_end];
            if (c == '[') {
                ++bracket_depth;
                ++name_end;
                continue;
            }
            if (c == ']') {
                --bracket_depth;
                ++name_end;
                continue;
            }
            if (bracket_depth > 0) {
                ++name_end;
                continue;
            }
            if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.') {
                ++name_end;
                continue;
            }
            break;
        }
        if (expr.compare(name_start, 7, "struct ") == 0) {
            name_end = name_start + 7;
            while (name_end < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[name_end])) != 0 ||
                    expr[name_end] == '_')) {
                ++name_end;
            }
        }
        if (!left_ok || name_end == name_start || name_end >= expr.size() ||
            expr[name_end] != '(') {
            pos = expr.find('*', pos + 1);
            continue;
        }
        int depth = 1;
        size_t cursor = name_end + 1;
        while (cursor < expr.size() && depth > 0) {
            if (expr[cursor] == '(') {
                ++depth;
            } else if (expr[cursor] == ')') {
                --depth;
            }
            ++cursor;
        }
        if (depth != 0) {
            break;
        }
        const std::string type = expr.substr(name_start, name_end - name_start);
        const std::string base_type =
            type.find('[') == std::string::npos ? type : type.substr(0, type.find('['));
        const bool type_like = std::find(builtin_pointer_types.begin(), builtin_pointer_types.end(),
                                         type) != builtin_pointer_types.end() ||
                               starts_with(type, "struct ") ||
                               type.find('[') != std::string::npos ||
                               type.find('.') != std::string::npos ||
                               std::isupper(static_cast<unsigned char>(base_type.front())) != 0;
        if (!type_like) {
            pos = expr.find('*', pos + 1);
            continue;
        }
        const std::string arg = expr.substr(name_end + 1, cursor - name_end - 2);
        const std::string replacement = "reinterpret_cast<" + lower_cpp_type("*" + type) + ">(" +
                                        lower_raw_cpp_escape_expr(arg) + ")";
        expr.replace(pos, cursor - pos, replacement);
        pos = expr.find('*', pos + replacement.size());
    }
    return expr;
}

std::string lower_enum_access(std::string expr) {
    size_t pos = expr.find('.');
    while (pos != std::string::npos) {
        size_t start = pos;
        while (start > 0 && (std::isalnum(static_cast<unsigned char>(expr[start - 1])) != 0 ||
                             expr[start - 1] == '_')) {
            --start;
        }
        if (start < pos && std::isupper(static_cast<unsigned char>(expr[start])) != 0) {
            expr.replace(pos, 1, "::");
            pos = expr.find('.', pos + 2);
            continue;
        }
        pos = expr.find('.', pos + 1);
    }
    return expr;
}

std::string lower_str_calls(std::string expr) {
    const std::string marker = "str(";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 && expr[pos - 1] != '_');
        if (left_ok) {
            expr.replace(pos, marker.size(), "std::to_string(");
            pos = expr.find(marker, pos + 15);
        } else {
            pos = expr.find(marker, pos + marker.size());
        }
    }
    return expr;
}

} // namespace dudu
