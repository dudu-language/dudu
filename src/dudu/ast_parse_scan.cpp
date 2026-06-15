#include "dudu/ast_parse_utils.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::vector<bool> quoted_positions(std::string_view text) {
    std::vector<bool> quoted(text.size(), false);
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote == '\0') {
            if (c == '"' || c == '\'') {
                quote = c;
                quoted[i] = true;
            }
            continue;
        }
        quoted[i] = true;
        if (escaped) {
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == quote) {
            quote = '\0';
        }
    }
    return quoted;
}

} // namespace

size_t find_top_level_colon_before_assign(std::string_view text) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}') {
            --brace_depth;
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0) {
            if (c == ':') {
                return i;
            }
            if (c == '=') {
                return std::string_view::npos;
            }
        }
    }
    return std::string_view::npos;
}

size_t find_top_level_assignment(std::string_view text, bool compound) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}') {
            --brace_depth;
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0 && c == '=') {
            const char prev = i == 0 ? '\0' : text[i - 1];
            const char next = i + 1 < text.size() ? text[i + 1] : '\0';
            if (next == '=') {
                continue;
            }
            const bool is_compound = prev == '+' || prev == '-' || prev == '*' || prev == '/' ||
                                     prev == '%' || prev == '|' || prev == '&' || prev == '^' ||
                                     prev == '<' || prev == '>';
            if (prev == '!') {
                continue;
            }
            if (compound == is_compound) {
                return i;
            }
        }
    }
    return std::string_view::npos;
}

size_t find_top_level_word(std::string_view text, std::string_view word) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}') {
            --brace_depth;
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0 &&
                   (i == 0 || !is_identifier_continue(text[i - 1])) &&
                   starts_keyword_exact(text.substr(i), word)) {
            return i;
        }
    }
    return std::string_view::npos;
}

size_t find_matching_open(std::string_view text, size_t close_pos, char open, char close) {
    int depth = 0;
    for (size_t pos = close_pos + 1; pos > 0; --pos) {
        const size_t i = pos - 1;
        if (text[i] == close) {
            ++depth;
        } else if (text[i] == open) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string_view::npos;
}

bool enclosed_by_outer_pair(std::string_view text, char open, char close) {
    text = trim_view(text);
    if (text.size() < 2 || text.front() != open || text.back() != close) {
        return false;
    }
    return find_matching_open(text, text.size() - 1, open, close) == 0;
}

CommaPart trim_comma_part(std::string_view text, size_t offset) {
    const size_t trim_start = text.find_first_not_of(" \t\r\n");
    if (trim_start == std::string_view::npos) {
        return {{}, offset + text.size()};
    }
    const size_t trim_end = text.find_last_not_of(" \t\r\n");
    return {text.substr(trim_start, trim_end - trim_start + 1), offset + trim_start};
}

std::vector<CommaPart> split_top_level_comma_parts(std::string_view text) {
    std::vector<CommaPart> parts;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
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
        if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}') {
            --brace_depth;
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0 && c == ',') {
            parts.push_back(trim_comma_part(text.substr(start, i - start), start));
            start = i + 1;
        }
    }
    parts.push_back(trim_comma_part(text.substr(start), start));
    return parts;
}

std::vector<std::string_view> split_top_level_commas(std::string_view text) {
    std::vector<std::string_view> parts;
    for (const CommaPart& part : split_top_level_comma_parts(text)) {
        parts.push_back(part.text);
    }
    return parts;
}

bool has_top_level_colon(std::string_view text) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (const char c : text) {
        if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}') {
            --brace_depth;
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0 && c == ':') {
            return true;
        }
    }
    return false;
}

size_t find_top_level_colon(std::string_view text) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    char quote = '\0';
    bool escaped = false;
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
        if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}') {
            --brace_depth;
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0 && c == ':') {
            return i;
        }
    }
    return std::string_view::npos;
}

size_t find_top_level_member_dot(std::string_view text) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    const std::vector<bool> quoted = quoted_positions(text);
    for (size_t pos = text.size(); pos > 0; --pos) {
        const size_t i = pos - 1;
        if (quoted[i]) {
            continue;
        }
        const char c = text[i];
        if (c == ']') {
            ++bracket_depth;
        } else if (c == '[') {
            --bracket_depth;
        } else if (c == ')') {
            ++paren_depth;
        } else if (c == '(') {
            --paren_depth;
        } else if (c == '}') {
            ++brace_depth;
        } else if (c == '{') {
            --brace_depth;
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0 && c == '.') {
            const char prev = i == 0 ? '\0' : text[i - 1];
            const char next = i + 1 < text.size() ? text[i + 1] : '\0';
            const bool numeric_decimal =
                std::isdigit(static_cast<unsigned char>(prev)) != 0 &&
                std::isdigit(static_cast<unsigned char>(next)) != 0;
            if (!numeric_decimal && is_identifier_start(next)) {
                return i;
            }
        }
    }
    return std::string_view::npos;
}

size_t find_top_level_binary_operator(std::string_view text,
                                      const std::vector<std::string_view>& ops) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    const std::vector<bool> quoted = quoted_positions(text);
    for (size_t pos = text.size(); pos > 0; --pos) {
        const size_t i = pos - 1;
        if (quoted[i]) {
            continue;
        }
        const char c = text[i];
        if (c == ']') {
            ++bracket_depth;
        } else if (c == '[') {
            --bracket_depth;
        } else if (c == ')') {
            ++paren_depth;
        } else if (c == '(') {
            --paren_depth;
        } else if (c == '}') {
            ++brace_depth;
        } else if (c == '{') {
            --brace_depth;
        }
        if (bracket_depth != 0 || paren_depth != 0 || brace_depth != 0) {
            continue;
        }
        for (const std::string_view op : ops) {
            if (i + op.size() > text.size() || text.substr(i, op.size()) != op) {
                continue;
            }
            if (i == 0) {
                continue;
            }
            if ((op == "and" || op == "or") && !((i == 0 || !is_identifier_continue(text[i - 1])) &&
                                                 starts_keyword_exact(text.substr(i), op))) {
                continue;
            }
            if ((op == "+" || op == "-") &&
                (i == 0 || text[i - 1] == '(' || text[i - 1] == '[' || text[i - 1] == ',')) {
                continue;
            }
            if (op == "<" &&
                ((i > 0 && text[i - 1] == '<') || (i + 1 < text.size() && text[i + 1] == '<'))) {
                continue;
            }
            if (op == ">" &&
                ((i > 0 && text[i - 1] == '>') || (i + 1 < text.size() && text[i + 1] == '>'))) {
                continue;
            }
            return i;
        }
    }
    return std::string_view::npos;
}

} // namespace dudu
