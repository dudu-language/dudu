#include "dudu/sema_scan.hpp"

#include <cctype>

namespace dudu {

size_t compound_assign_pos(const std::string& text, size_t assign) {
    if (assign == std::string::npos) {
        return std::string::npos;
    }
    size_t pos = assign;
    while (pos > 0 && std::isspace(static_cast<unsigned char>(text[pos - 1])) != 0) {
        --pos;
    }
    if (pos == 0) {
        return std::string::npos;
    }
    if (pos >= 2) {
        const std::string two = text.substr(pos - 2, 2);
        if (two == "<<" || two == ">>") {
            return pos - 2;
        }
    }
    const char op = text[pos - 1];
    if (op == '+' || op == '-' || op == '*' || op == '/' || op == '%' || op == '^' || op == '&' ||
        op == '|') {
        return pos - 1;
    }
    return std::string::npos;
}

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

size_t find_top_level_comparison(const std::string& expr) {
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
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            --depth;
            continue;
        }
        if (depth != 0) {
            continue;
        }
        const char next = i + 1 < expr.size() ? expr[i + 1] : '\0';
        const char prev = i > 0 ? expr[i - 1] : '\0';
        if (c == '=' && next == '=') {
            return i;
        }
        if (c == '!' && next == '=') {
            return i;
        }
        if (c == '<' && next != '<' && prev != '<') {
            return i;
        }
        if (c == '>' && next != '>' && prev != '>') {
            return i;
        }
    }
    return std::string::npos;
}

size_t find_top_level_operator(const std::string& expr) {
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
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            --depth;
            continue;
        }
        if (depth == 0 && i > 0 &&
            (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^' || c == '&' ||
             c == '|' || c == '<' || c == '>')) {
            return i;
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
