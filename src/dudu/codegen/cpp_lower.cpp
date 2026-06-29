#include "dudu/codegen/cpp_lower.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_type_internal.hpp"

#include <cctype>
#include <map>
#include <sstream>

namespace dudu {

std::string replace_dots(std::string text) {
    size_t pos = 0;
    while ((pos = text.find('.', pos)) != std::string::npos) {
        text.replace(pos, 1, "::");
        pos += 2;
    }
    return text;
}

std::string trim_copy(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool is_non_type_template_arg(const std::string& text) {
    const std::string trimmed = trim_copy(text);
    if (trimmed.empty()) {
        return false;
    }
    size_t start = trimmed.front() == '-' ? 1 : 0;
    if (start >= trimmed.size()) {
        return false;
    }
    for (size_t i = start; i < trimmed.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(trimmed[i])) == 0) {
            return false;
        }
    }
    return true;
}

std::string unescape_cpp_string(std::string text) {
    std::string out;
    out.reserve(text.size());
    bool escaped = false;
    for (const char c : text) {
        if (!escaped && c == '\\') {
            escaped = true;
            continue;
        }
        if (escaped) {
            out.push_back(c == 'n' ? '\n' : c == 't' ? '\t' : c);
            escaped = false;
            continue;
        }
        out.push_back(c);
    }
    if (escaped) {
        out.push_back('\\');
    }
    return out;
}

std::string cpp_escape_body(std::string text) {
    text = trim_copy(std::move(text));
    if (!starts_with(text, "cpp(") || text.back() != ')') {
        return {};
    }
    text = trim_copy(text.substr(4, text.size() - 5));
    if (starts_with(text, "\"\"\"") && ends_with(text, "\"\"\"")) {
        return text.substr(3, text.size() - 6);
    }
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return unescape_cpp_string(text.substr(1, text.size() - 2));
    }
    return {};
}

std::vector<std::string> cpp_escape_lines(std::string body_text) {
    std::vector<std::string> lines;
    std::istringstream body(std::move(body_text));
    std::string line;
    while (std::getline(body, line)) {
        line = trim_copy(std::move(line));
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

std::string lower_raw_template_call_arg(const std::string& arg,
                                        const std::vector<std::string>& namespace_aliases) {
    const std::string trimmed = trim_copy(arg);
    if (is_non_type_template_arg(trimmed)) {
        return trimmed;
    }
    if (starts_with(trimmed, "fn(")) {
        return lower_cpp_function_type(parse_type_text(trimmed), false, namespace_aliases);
    }
    return lower_cpp_type_spelling(trimmed, namespace_aliases);
}

std::vector<std::string> split_top_level_args(const std::string& args) {
    std::vector<std::string> out;
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t start = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        const char c = args[i];
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
            out.push_back(trim_copy(args.substr(start, i - start)));
            start = i + 1;
        }
    }
    const std::string last = trim_copy(args.substr(start));
    if (!last.empty()) {
        out.push_back(last);
    }
    return out;
}

std::string replace_all(std::string text, std::string_view from, std::string_view to) {
    size_t pos = text.find(from);
    while (pos != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos = text.find(from, pos + to.size());
    }
    return text;
}

std::string qualify_namespace_aliases(std::string expr,
                                      const std::vector<std::string>& namespace_aliases) {
    for (const std::string& alias : namespace_aliases) {
        const bool strip_alias = !alias.empty() && alias.front() == '!';
        const std::string name = strip_alias ? alias.substr(1) : alias;
        if (strip_alias && name.find('.') != std::string::npos) {
            const std::string marker = name + ".";
            size_t prefix_pos = expr.find(marker);
            while (prefix_pos != std::string::npos) {
                const bool left_ok =
                    prefix_pos == 0 ||
                    (std::isalnum(static_cast<unsigned char>(expr[prefix_pos - 1])) == 0 &&
                     expr[prefix_pos - 1] != '_');
                if (left_ok) {
                    expr.erase(prefix_pos, marker.size());
                    prefix_pos = expr.find(marker, prefix_pos);
                } else {
                    prefix_pos = expr.find(marker, prefix_pos + marker.size());
                }
            }
            size_t pos = expr.find(name);
            while (pos != std::string::npos) {
                const bool left_ok =
                    pos == 0 || (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 &&
                                 expr[pos - 1] != '_');
                const size_t after = pos + name.size();
                const bool right_ok = after >= expr.size() ||
                                      (std::isalnum(static_cast<unsigned char>(expr[after])) == 0 &&
                                       expr[after] != '_' && expr[after] != '.');
                if (left_ok && right_ok) {
                    const size_t dot = name.rfind('.');
                    const std::string replacement = name.substr(dot + 1);
                    expr.replace(pos, name.size(), replacement);
                    pos = expr.find(name, pos + replacement.size());
                } else {
                    pos = expr.find(name, pos + name.size());
                }
            }
            continue;
        }
        const std::string marker = name + ".";
        size_t pos = expr.find(marker);
        while (pos != std::string::npos) {
            const bool left_ok =
                pos == 0 || (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 &&
                             expr[pos - 1] != '_');
            if (left_ok) {
                size_t end = pos + marker.size();
                while (end < expr.size() &&
                       (std::isalnum(static_cast<unsigned char>(expr[end])) != 0 ||
                        expr[end] == '_' || expr[end] == '.')) {
                    ++end;
                }
                std::string replacement =
                    strip_alias ? expr.substr(pos + marker.size(), end - pos - marker.size())
                                : expr.substr(pos, end - pos);
                replacement = replace_all(std::move(replacement), ".", "::");
                expr.replace(pos, end - pos, replacement);
                pos = expr.find(marker, pos + replacement.size());
            } else {
                pos = expr.find(marker, pos + marker.size());
            }
        }
    }
    return expr;
}

} // namespace dudu
