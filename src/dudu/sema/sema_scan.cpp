#include "dudu/sema/sema_scan.hpp"

#include <cctype>

namespace dudu {

size_t find_call_open(const std::string& expr) {
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < expr.size(); ++i) {
        const char c = expr[i];
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
        if (c == '(' && depth == 0) {
            return i;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        }
    }
    return std::string::npos;
}

size_t find_call_close(const std::string& expr, size_t open) {
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t i = open; i < expr.size(); ++i) {
        const char c = expr[i];
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
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

bool is_plain_identifier(const std::string& expr) {
    if (expr.empty() ||
        (std::isalpha(static_cast<unsigned char>(expr.front())) == 0 && expr.front() != '_')) {
        return false;
    }
    for (const char c : expr) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
            return false;
        }
    }
    return true;
}

} // namespace dudu
