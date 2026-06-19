#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_raw_escape_internal.hpp"

#include <cctype>
#include <sstream>

namespace dudu {

size_t raw_escape_find_matching(std::string_view text, const size_t open, const char left,
                                const char right) {
    int depth = 1;
    char quote = '\0';
    bool escaped = false;
    size_t cursor = open + 1;
    while (cursor < text.size() && depth > 0) {
        const char c = text[cursor];
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
        } else if (c == left) {
            ++depth;
        } else if (c == right) {
            --depth;
        }
        ++cursor;
    }
    return depth == 0 ? cursor - 1 : std::string::npos;
}

namespace {

bool namespace_qualified_name(const std::string& name,
                              const std::vector<std::string>& namespace_aliases) {
    const size_t dot = name.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string head = name.substr(0, dot);
    for (const std::string& alias : namespace_aliases) {
        const std::string clean = !alias.empty() && alias.front() == '!' ? alias.substr(1) : alias;
        if (clean == head) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string lower_template_alloc_call(std::string expr, std::string_view name) {
    const std::string marker = std::string(name) + "[";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const size_t type_start = pos + marker.size();
        const size_t type_end = expr.find(']', type_start);
        if (type_end == std::string::npos || type_end + 1 >= expr.size() ||
            expr[type_end + 1] != '(') {
            pos = expr.find(marker, pos + marker.size());
            continue;
        }
        const size_t args_start = type_end + 2;
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
        const std::string type = expr.substr(type_start, type_end - type_start);
        const std::string args = expr.substr(args_start, cursor - args_start - 1);
        std::string replacement;
        if (name == "new") {
            replacement = "new " + lower_cpp_type_spelling(type) + "(" + args + ")";
        } else {
            const std::string lowered = lower_cpp_type_spelling(type);
            replacement = "static_cast<" + lowered + "*>(std::malloc(sizeof(" + lowered + ") * (" +
                          args + ")))";
        }
        expr.replace(pos, cursor - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
}

std::string lower_template_value_call(std::string expr, std::string_view name) {
    const std::string marker = std::string(name) + "[";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const size_t args_start = pos + marker.size();
        const size_t args_end = expr.find(']', args_start);
        if (args_end == std::string::npos || args_end + 2 > expr.size() ||
            expr.substr(args_end + 1, 2) != "()") {
            pos = expr.find(marker, pos + marker.size());
            continue;
        }
        const std::string type = expr.substr(pos, args_end - pos + 1);
        const std::string replacement = lower_cpp_type_spelling(type) + "{}";
        expr.replace(pos, args_end + 3 - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
}

std::string lower_type_operator_call(std::string expr, std::string_view name) {
    const std::string marker = std::string(name) + "[";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const size_t type_start = pos + marker.size();
        const size_t type_end = expr.find(']', type_start);
        if (type_end == std::string::npos || type_end + 2 > expr.size() ||
            expr.substr(type_end + 1, 2) != "()") {
            pos = expr.find(marker, pos + marker.size());
            continue;
        }
        const std::string type = expr.substr(type_start, type_end - type_start);
        const std::string replacement =
            std::string(name) + "(" + lower_cpp_type_spelling(type) + ")";
        expr.replace(pos, type_end + 3 - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
}

std::string lower_offsetof_call(std::string expr) {
    const std::string marker = "offsetof[";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const size_t type_start = pos + marker.size();
        const size_t type_end = expr.find(']', type_start);
        if (type_end == std::string::npos || type_end + 1 >= expr.size() ||
            expr[type_end + 1] != '(') {
            pos = expr.find(marker, pos + marker.size());
            continue;
        }
        const size_t field_start = type_end + 2;
        const size_t field_end = expr.find(')', field_start);
        if (field_end == std::string::npos) {
            break;
        }
        const std::string type = expr.substr(type_start, type_end - type_start);
        const std::string field = trim_copy(expr.substr(field_start, field_end - field_start));
        const std::string replacement =
            "offsetof(" + lower_cpp_type_spelling(type) + ", " + field + ")";
        expr.replace(pos, field_end + 1 - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
}

std::string lower_dotted_template_call(std::string expr,
                                       const std::vector<std::string>& namespace_aliases) {
    size_t open = expr.find('[');
    while (open != std::string::npos) {
        const size_t close = raw_escape_find_matching(expr, open, '[', ']');
        if (close == std::string::npos || close + 1 >= expr.size() || expr[close + 1] != '(') {
            open = expr.find('[', open + 1);
            continue;
        }
        const size_t args_close = raw_escape_find_matching(expr, close + 1, '(', ')');
        if (args_close == std::string::npos) {
            break;
        }
        size_t name_start = open;
        while (name_start > 0) {
            const char c = expr[name_start - 1];
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_' && c != '.' &&
                c != ':') {
                break;
            }
            --name_start;
        }
        const std::string name = expr.substr(name_start, open - name_start);
        if (name.find('.') == std::string::npos) {
            open = expr.find('[', open + 1);
            continue;
        }
        const std::string arg = expr.substr(open + 1, close - open - 1);
        const std::string call_args = expr.substr(close + 2, args_close - close - 2);
        const std::string lowered_args = lower_raw_cpp_escape_expr(call_args, namespace_aliases);
        std::ostringstream lowered_template_args;
        const std::vector<std::string> template_args = split_top_level_args(arg);
        for (size_t i = 0; i < template_args.size(); ++i) {
            if (i > 0) {
                lowered_template_args << ", ";
            }
            lowered_template_args << lower_template_call_arg(template_args[i], namespace_aliases);
        }
        const std::string lowered_template =
            (namespace_qualified_name(name, namespace_aliases) ? replace_dots(name) : name) + "<" +
            lowered_template_args.str() + ">";
        const std::string replacement = lowered_template + "(" + lowered_args + ")";
        expr.replace(name_start, args_close + 1 - name_start, replacement);
        open = expr.find('[', name_start + replacement.size());
    }
    return expr;
}

} // namespace dudu
