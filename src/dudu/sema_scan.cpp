#include "dudu/sema_scan.hpp"

#include <cctype>
#include <string_view>

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

bool word_at(const std::string& expr, size_t pos, std::string_view word) {
    if (pos + word.size() > expr.size() || expr.substr(pos, word.size()) != word) {
        return false;
    }
    const bool before = pos == 0 || (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 &&
                                     expr[pos - 1] != '_');
    const size_t after_pos = pos + word.size();
    const bool after =
        after_pos == expr.size() ||
        (std::isalnum(static_cast<unsigned char>(expr[after_pos])) == 0 && expr[after_pos] != '_');
    return before && after;
}

size_t find_top_level_logical(const std::string& expr) {
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
        if (depth == 0 && (word_at(expr, i, "and") || word_at(expr, i, "or"))) {
            return i;
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

std::string top_level_comparison_text(const std::string& expr, size_t pos) {
    if (pos + 1 < expr.size()) {
        const std::string two = expr.substr(pos, 2);
        if (two == "==" || two == "!=" || two == "<=" || two == ">=") {
            return two;
        }
    }
    return pos < expr.size() ? expr.substr(pos, 1) : std::string{};
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
