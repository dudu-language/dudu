#include "dudu/cpp_lower.hpp"

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
