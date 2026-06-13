#include "dudu/cpp_lower.hpp"

#include <algorithm>
#include <cctype>

namespace dudu {
namespace {

size_t find_top_level_word(const std::string& text, std::string_view word) {
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t pos = text.find(word);
    while (pos != std::string::npos) {
        depth = 0;
        quote = '\0';
        escaped = false;
        for (size_t i = 0; i < pos; ++i) {
            const char c = text[i];
            if (quote != '\0') {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == quote) {
                    quote = '\0';
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(' || c == '[' || c == '{') {
                ++depth;
            } else if (c == ')' || c == ']' || c == '}') {
                --depth;
            }
        }
        if (quote == '\0' && depth == 0) {
            return pos;
        }
        pos = text.find(word, pos + word.size());
    }
    return std::string::npos;
}

} // namespace

std::string lower_cpp_expr(std::string expr) {
    return lower_cpp_expr(expr, {});
}

std::string lower_conditional_expr(std::string expr) {
    const size_t if_pos = find_top_level_word(expr, " if ");
    if (if_pos == std::string::npos) {
        return expr;
    }
    const size_t else_pos = find_top_level_word(expr.substr(if_pos + 4), " else ");
    if (else_pos == std::string::npos) {
        return expr;
    }
    const size_t absolute_else = if_pos + 4 + else_pos;
    const std::string when_true = trim_copy(expr.substr(0, if_pos));
    const std::string condition = trim_copy(expr.substr(if_pos + 4, absolute_else - if_pos - 4));
    const std::string when_false = trim_copy(expr.substr(absolute_else + 6));
    return "(" + lower_cpp_expr(condition) + " ? " + lower_cpp_expr(when_true) + " : " +
           lower_cpp_expr(when_false) + ")";
}

std::string lower_lambda_expr(std::string expr) {
    const std::string marker = "lambda ";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const size_t args_start = pos + marker.size();
        const size_t colon = expr.find(':', args_start);
        if (colon == std::string::npos) {
            return expr;
        }
        const std::vector<std::string> args =
            split_top_level_args(expr.substr(args_start, colon - args_start));
        size_t body_end = colon + 1;
        int depth = 0;
        while (body_end < expr.size()) {
            const char c = expr[body_end];
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            } else if (c == ')' || c == ']' || c == '}') {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (c == ',' && depth == 0) {
                break;
            }
            ++body_end;
        }
        std::string replacement = "[&](";
        for (size_t i = 0; i < args.size(); ++i) {
            replacement += (i == 0 ? "" : ", ") + std::string("auto&& ") + args[i];
        }
        replacement +=
            ") { return " + lower_cpp_expr(expr.substr(colon + 1, body_end - colon - 1)) + "; }";
        expr.replace(pos, body_end - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
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
        const std::string replacement = lower_cpp_type(type) + "(" + lower_cpp_expr(args) + ")";
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
        const std::string replacement = "(" + lower_cpp_expr(arg) + ").size()";
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
        const std::string replacement =
            "reinterpret_cast<" + lower_cpp_type("*" + type) + ">(" + lower_cpp_expr(arg) + ")";
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

std::string lower_str_from_cstr(std::string expr) {
    const std::string marker = "str.from_cstr(";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const size_t args_start = pos + marker.size();
        int depth = 1;
        size_t cursor = args_start;
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
        const std::string arg = expr.substr(args_start, cursor - args_start - 1);
        const std::string replacement =
            "std::string(reinterpret_cast<const char*>(" + lower_cpp_expr(arg) + "))";
        expr.replace(pos, cursor - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
}

} // namespace dudu
