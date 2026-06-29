#include "dudu/parser/ast_parse_utils.hpp"

#include <string>
#include <vector>

namespace dudu {

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

} // namespace dudu
