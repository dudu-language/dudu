#include "dudu/cpp_lower.hpp"

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
        if (c == '[' || c == '(') {
            ++depth;
        } else if (c == ']' || c == ')') {
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

size_t top_level_equal(const std::string& text) {
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[' || c == '(' || c == '{') {
            ++depth;
        } else if (c == ']' || c == ')' || c == '}') {
            --depth;
        } else if (c == '=' && depth == 0) {
            if ((i > 0 && text[i - 1] == '=') || (i + 1 < text.size() && text[i + 1] == '=')) {
                continue;
            }
            return i;
        }
    }
    return std::string::npos;
}

std::string lower_cpp_expr(std::string expr);

std::string replace_word(std::string text, std::string_view from, std::string_view to) {
    size_t pos = text.find(from);
    while (pos != std::string::npos) {
        char quote = '\0';
        bool escaped = false;
        bool in_string = false;
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
            }
        }
        in_string = quote != '\0';
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(text[pos - 1])) == 0 && text[pos - 1] != '_');
        const size_t end = pos + from.size();
        const bool right_ok =
            end == text.size() ||
            (std::isalnum(static_cast<unsigned char>(text[end])) == 0 && text[end] != '_');
        if (!in_string && left_ok && right_ok) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        } else {
            pos += from.size();
        }
        pos = text.find(from, pos);
    }
    return text;
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
        const std::string marker = name + ".";
        size_t pos = expr.find(marker);
        while (pos != std::string::npos) {
            const bool left_ok =
                pos == 0 || (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 &&
                             expr[pos - 1] != '_');
            if (left_ok) {
                const std::string replacement = strip_alias ? "" : name + "::";
                expr.replace(pos, marker.size(), replacement);
                pos = expr.find(marker, pos + replacement.size());
            } else {
                pos = expr.find(marker, pos + marker.size());
            }
        }
    }
    return expr;
}

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
            replacement = "new " + lower_cpp_type(type) + "(" + args + ")";
        } else {
            const std::string lowered = lower_cpp_type(type);
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
        const std::string replacement = lower_cpp_type(type) + "{}";
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
        const std::string replacement = std::string(name) + "(" + lower_cpp_type(type) + ")";
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
        const std::string replacement = "offsetof(" + lower_cpp_type(type) + ", " + field + ")";
        expr.replace(pos, field_end + 1 - pos, replacement);
        pos = expr.find(marker, pos + replacement.size());
    }
    return expr;
}

std::string lower_builtin_cast_calls(std::string expr) {
    static const std::map<std::string, std::string> casts = {
        {"i8", "int8_t"},      {"i16", "int16_t"},  {"i32", "int32_t"},  {"i64", "int64_t"},
        {"u8", "uint8_t"},     {"u16", "uint16_t"}, {"u32", "uint32_t"}, {"u64", "uint64_t"},
        {"isize", "intptr_t"}, {"usize", "size_t"}, {"f32", "float"},    {"f64", "double"}};
    for (const auto& [from, to] : casts) {
        const std::string marker = from + "(";
        size_t pos = expr.find(marker);
        while (pos != std::string::npos) {
            const bool left_ok =
                pos == 0 || (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 &&
                             expr[pos - 1] != '_');
            if (left_ok) {
                expr.replace(pos, marker.size(), to + "(");
                pos = expr.find(marker, pos + to.size() + 1);
            } else {
                pos = expr.find(marker, pos + marker.size());
            }
        }
    }
    return expr;
}

std::string lower_named_argument_calls(std::string expr) {
    size_t open = expr.find('(');
    while (open != std::string::npos) {
        int depth = 1;
        size_t close = open + 1;
        while (close < expr.size() && depth > 0) {
            if (expr[close] == '(') {
                ++depth;
            } else if (expr[close] == ')') {
                --depth;
            }
            ++close;
        }
        if (depth != 0) {
            break;
        }
        const size_t close_pos = close - 1;
        size_t name_start = open;
        while (name_start > 0) {
            const char c = expr[name_start - 1];
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_' && c != '.') {
                break;
            }
            --name_start;
        }
        if (name_start == open) {
            open = expr.find('(', open + 1);
            continue;
        }

        const std::string args = expr.substr(open + 1, close_pos - open - 1);
        const std::vector<std::string> parts = split_top_level_args(args);
        bool has_named_arg = false;
        for (const std::string& part : parts) {
            has_named_arg = has_named_arg || top_level_equal(part) != std::string::npos;
        }
        if (!has_named_arg) {
            open = expr.find('(', open + 1);
            continue;
        }

        std::ostringstream replacement;
        replacement << expr.substr(name_start, open - name_start) << "{";
        for (size_t i = 0; i < parts.size(); ++i) {
            const size_t equal = top_level_equal(parts[i]);
            if (equal == std::string::npos) {
                replacement << (i == 0 ? "" : ", ") << lower_cpp_expr(parts[i]);
                continue;
            }
            if (i > 0) {
                replacement << ", ";
            }
            replacement << "." << trim_copy(parts[i].substr(0, equal)) << " = "
                        << lower_cpp_expr(parts[i].substr(equal + 1));
        }
        replacement << "}";
        const std::string text = replacement.str();
        expr.replace(name_start, close_pos + 1 - name_start, text);
        open = expr.find('(', name_start + text.size());
    }
    return expr;
}

std::string lower_dotted_template_call(std::string expr) {
    size_t open = expr.find('[');
    while (open != std::string::npos) {
        const size_t close = expr.find(']', open + 1);
        if (close == std::string::npos || close + 2 > expr.size() ||
            expr.substr(close + 1, 2) != "()") {
            open = expr.find('[', open + 1);
            continue;
        }
        size_t name_start = open;
        while (name_start > 0) {
            const char c = expr[name_start - 1];
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_' && c != '.') {
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
        const std::string replacement = replace_dots(name) + "<" + lower_cpp_type(arg) + ">()";
        expr.replace(name_start, close + 3 - name_start, replacement);
        open = expr.find('[', name_start + replacement.size());
    }
    return expr;
}

std::string lower_cpp_expr(std::string expr) {
    return lower_cpp_expr(std::move(expr), {});
}

std::string lower_cpp_expr(std::string expr, const std::vector<std::string>& namespace_aliases) {
    expr = lower_lambda_expr(std::move(expr));
    expr = lower_conditional_expr(std::move(expr));
    expr = lower_template_alloc_call(std::move(expr), "new");
    expr = lower_template_alloc_call(std::move(expr), "malloc");
    expr = lower_type_operator_call(std::move(expr), "sizeof");
    expr = lower_type_operator_call(std::move(expr), "alignof");
    expr = lower_offsetof_call(std::move(expr));
    expr = lower_builtin_cast_calls(std::move(expr));
    expr = lower_str_from_cstr(std::move(expr));
    expr = lower_str_calls(std::move(expr));
    expr = lower_named_argument_calls(std::move(expr));
    expr = lower_template_value_call(std::move(expr), "list");
    expr = lower_template_value_call(std::move(expr), "dict");
    expr = lower_template_value_call(std::move(expr), "set");
    expr = lower_dotted_template_call(std::move(expr));
    expr = qualify_namespace_aliases(std::move(expr), namespace_aliases);
    expr = replace_all(std::move(expr), ".append(", ".push_back(");
    expr = replace_all(std::move(expr), "Ok(None)", "dudu::Ok(std::monostate{})");
    expr = replace_word(std::move(expr), "Ok", "dudu::Ok");
    expr = replace_word(std::move(expr), "Err", "dudu::Err");
    expr = replace_word(std::move(expr), "True", "true");
    expr = replace_word(std::move(expr), "False", "false");
    expr = replace_word(std::move(expr), "None", "nullptr");
    expr = replace_word(std::move(expr), "and", "&&");
    expr = replace_word(std::move(expr), "or", "||");
    expr = replace_word(std::move(expr), "not", "!");
    return expr;
}

} // namespace dudu
