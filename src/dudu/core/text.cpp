#include "dudu/core/text.hpp"

namespace dudu {
namespace {

bool is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

} // namespace

std::string_view trim_view(std::string_view text) {
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string trim_string(std::string_view text) {
    return std::string(trim_view(text));
}

bool is_identifier_continue(char c) {
    return is_ascii_alpha(c) || is_ascii_digit(c) || c == '_';
}

bool is_identifier_start(char c) {
    return is_ascii_alpha(c) || c == '_';
}

bool is_identifier(std::string_view text) {
    text = trim_view(text);
    if (text.empty() || !is_identifier_start(text.front())) {
        return false;
    }
    for (const char c : text.substr(1)) {
        if (!is_identifier_continue(c)) {
            return false;
        }
    }
    return true;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.starts_with(prefix);
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.ends_with(suffix);
}

std::vector<std::string> split_top_level_args(std::string_view text) {
    std::vector<std::string> out;
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
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
        if (c == '[' || c == '(' || c == '{') {
            ++depth;
        } else if (c == ']' || c == ')' || c == '}') {
            --depth;
        } else if (c == ',' && depth == 0) {
            out.push_back(trim_string(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    const std::string last = trim_string(text.substr(start));
    if (!last.empty()) {
        out.push_back(last);
    }
    return out;
}

} // namespace dudu
