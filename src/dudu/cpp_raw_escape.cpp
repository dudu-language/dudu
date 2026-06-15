#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_raw_escape_internal.hpp"

#include <cctype>
#include <map>
#include <sstream>

namespace dudu {
namespace {

size_t top_level_equal(const std::string& text) {
    int depth = 0;
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
        if (c == '[' || c == '(' || c == '{') {
            ++depth;
        } else if (c == ']' || c == ')' || c == '}') {
            --depth;
        } else if (c == '=' && depth == 0) {
            const bool comparison =
                (i > 0 && std::string_view("=!<>").find(text[i - 1]) != std::string_view::npos) ||
                (i + 1 < text.size() && text[i + 1] == '=');
            if (comparison) {
                continue;
            }
            return i;
        }
    }
    return std::string::npos;
}

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
            pos == 0 || (std::isalnum(static_cast<unsigned char>(text[pos - 1])) == 0 &&
                         text[pos - 1] != '_' && text[pos - 1] != ':');
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
                replacement << (i == 0 ? "" : ", ") << lower_raw_cpp_escape_expr(parts[i]);
                continue;
            }
            if (i > 0) {
                replacement << ", ";
            }
            replacement << "." << trim_copy(parts[i].substr(0, equal)) << " = "
                        << lower_raw_cpp_escape_expr(parts[i].substr(equal + 1));
        }
        replacement << "}";
        const std::string text = replacement.str();
        expr.replace(name_start, close_pos + 1 - name_start, text);
        open = expr.find('(', name_start + text.size());
    }
    return expr;
}

std::string lower_comma_indexing(std::string expr) {
    size_t open = expr.find('[');
    while (open != std::string::npos) {
        const size_t close = raw_escape_find_matching(expr, open, '[', ']');
        if (close == std::string::npos) {
            break;
        }
        if (close + 1 < expr.size() && expr[close + 1] == '(') {
            open = expr.find('[', close + 1);
            continue;
        }
        const std::vector<std::string> parts =
            split_top_level_args(expr.substr(open + 1, close - open - 1));
        if (parts.size() <= 1) {
            open = expr.find('[', close + 1);
            continue;
        }
        std::ostringstream replacement;
        for (const std::string& part : parts) {
            replacement << "[" << trim_copy(part) << "]";
        }
        const std::string text = replacement.str();
        expr.replace(open, close + 1 - open, text);
        open = expr.find('[', open + text.size());
    }
    return expr;
}

size_t top_level_colon_in_text(const std::string& text) {
    int depth = 0;
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
        if (c == '[' || c == '(' || c == '{') {
            ++depth;
        } else if (c == ']' || c == ')' || c == '}') {
            --depth;
        } else if (c == ':' && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

std::string lower_slice_indexing(std::string expr) {
    size_t open = expr.find('[');
    while (open != std::string::npos) {
        const size_t close = raw_escape_find_matching(expr, open, '[', ']');
        if (close == std::string::npos) {
            break;
        }
        const std::string inside = expr.substr(open + 1, close - open - 1);
        const size_t colon = top_level_colon_in_text(inside);
        if (colon == std::string::npos) {
            open = expr.find('[', close + 1);
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
        if (name_start == open) {
            open = expr.find('[', close + 1);
            continue;
        }
        const std::string target = expr.substr(name_start, open - name_start);
        const std::string start = trim_copy(inside.substr(0, colon));
        const std::string end = trim_copy(inside.substr(colon + 1));
        if (start.empty() || end.empty()) {
            open = expr.find('[', close + 1);
            continue;
        }
        const std::string replacement =
            "std::span(&(" + target + ")[" + lower_raw_cpp_escape_expr(start) + "], (" +
            lower_raw_cpp_escape_expr(end) + ") - (" + lower_raw_cpp_escape_expr(start) + "))";
        expr.replace(name_start, close + 1 - name_start, replacement);
        open = expr.find('[', name_start + replacement.size());
    }
    return expr;
}

} // namespace

std::string lower_raw_cpp_escape_expr(std::string expr,
                                      const std::vector<std::string>& namespace_aliases) {
    expr = lower_template_alloc_call(std::move(expr), "new");
    expr = lower_template_alloc_call(std::move(expr), "malloc");
    expr = lower_generic_type_constructor(std::move(expr));
    expr = lower_type_operator_call(std::move(expr), "sizeof");
    expr = lower_type_operator_call(std::move(expr), "alignof");
    expr = lower_offsetof_call(std::move(expr));
    expr = lower_pointer_cast_calls(std::move(expr));
    expr = lower_builtin_cast_calls(std::move(expr));
    expr = lower_len_calls(std::move(expr));
    expr = lower_numeric_separators(std::move(expr));
    expr = lower_str_calls(std::move(expr));
    expr = lower_named_argument_calls(std::move(expr));
    expr = lower_enum_access(std::move(expr));
    expr = lower_dotted_template_call(std::move(expr), namespace_aliases);
    expr = lower_template_value_call(std::move(expr), "list");
    expr = lower_template_value_call(std::move(expr), "dict");
    expr = lower_template_value_call(std::move(expr), "set");
    expr = lower_slice_indexing(std::move(expr));
    expr = lower_comma_indexing(std::move(expr));
    expr = qualify_namespace_aliases(std::move(expr), namespace_aliases);
    expr = replace_all(std::move(expr), ".append(", ".push_back(");
    expr = replace_all(std::move(expr), "push_back([])", "push_back({})");
    expr = replace_all(std::move(expr), "build.", "build::");
    expr = replace_all(std::move(expr), "shader.", "shader::");
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
