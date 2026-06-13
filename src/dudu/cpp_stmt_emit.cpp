#include "dudu/cpp_stmt_emit.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_pointer_members.hpp"
#include "dudu/cpp_stmt_types.hpp"

#include <map>
#include <sstream>

namespace dudu {
namespace {

std::string indent(int depth) {
    return std::string(static_cast<size_t>(depth) * 4, ' ');
}

std::string lower_expr(std::string expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals) {
    return lower_cpp_expr(rewrite_pointer_members(std::move(expr), locals), aliases);
}

size_t find_top_level_assignment(const std::string& text) {
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
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == '=' && depth == 0) {
            if ((i > 0 && std::string("=!<>+-*/%^&|").find(text[i - 1]) != std::string::npos) ||
                (i + 1 < text.size() && text[i + 1] == '=')) {
                continue;
            }
            return i;
        }
    }
    return std::string::npos;
}

size_t find_top_level_colon(const std::string& text) {
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
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == ':' && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

std::string strip_trailing_colon(std::string text) {
    text = trim_copy(std::move(text));
    if (!text.empty() && text.back() == ':') {
        text.pop_back();
    }
    return trim_copy(std::move(text));
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_tuple_literal(const std::string& value) {
    if (split_top_level_args(value).size() > 1) {
        return true;
    }
    return starts_with(value, "(") && ends_with(value, ")") &&
           split_top_level_args(value.substr(1, value.size() - 2)).size() > 1;
}

std::string tuple_literal_body(const std::string& value) {
    return starts_with(value, "(") && ends_with(value, ")") ? value.substr(1, value.size() - 2)
                                                            : value;
}

std::string lower_literal_value(const std::string& value, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals);

std::string lower_dict_literal_body(const std::string& value,
                                    const std::vector<std::string>& aliases,
                                    const std::map<std::string, std::string>& locals) {
    const std::string body = value.substr(1, value.size() - 2);
    std::ostringstream out;
    const std::vector<std::string> entries = split_top_level_args(body);
    for (size_t i = 0; i < entries.size(); ++i) {
        const size_t colon = find_top_level_colon(entries[i]);
        if (colon == std::string::npos) {
            continue;
        }
        if (i > 0) {
            out << ", ";
        }
        out << "{" << lower_expr(entries[i].substr(0, colon), aliases, locals) << ", ";
        out << lower_literal_value(entries[i].substr(colon + 1), aliases, locals) << "}";
    }
    return out.str();
}

bool is_dict_literal_value(std::string value) {
    value = trim_copy(std::move(value));
    if (!starts_with(value, "{") || !ends_with(value, "}")) {
        return false;
    }
    for (const std::string& entry : split_top_level_args(value.substr(1, value.size() - 2))) {
        if (find_top_level_colon(entry) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string lower_literal_value(const std::string& value, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals) {
    const std::string trimmed = trim_copy(value);
    if (is_dict_literal_value(trimmed)) {
        return "{" + lower_dict_literal_body(trimmed, aliases, locals) + "}";
    }
    return lower_expr(trimmed, aliases, locals);
}

bool is_build_only_condition(const std::string& text) {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            ++i;
            continue;
        }
        if (!is_identifier_char(c)) {
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < text.size() && is_identifier_char(text[i])) {
            ++i;
        }
        const std::string word = text.substr(start, i - start);
        if (word == "True" || word == "False" || word == "and" || word == "or" || word == "not") {
            continue;
        }
        if (word == "build" && i < text.size() && text[i] == '.') {
            ++i;
            if (i >= text.size() || !is_identifier_char(text[i])) {
                return false;
            }
            while (i < text.size() && is_identifier_char(text[i])) {
                ++i;
            }
            continue;
        }
        return false;
    }
    return text.find("build.") != std::string::npos;
}

std::string if_keyword_for_condition(const std::string& condition) {
    return is_build_only_condition(condition) ? "if constexpr" : "if";
}

std::string normalize_spaced_compound(std::string text) {
    for (const std::pair<std::string_view, std::string_view> op :
         {std::pair{" + =", " +="}, std::pair{" - =", " -="}, std::pair{" * =", " *="},
          std::pair{" / =", " /="}, std::pair{" % =", " %="}, std::pair{" ^ =", " ^="},
          std::pair{" & =", " &="}, std::pair{" | =", " |="}}) {
        size_t pos = text.find(op.first);
        while (pos != std::string::npos) {
            text.replace(pos, op.first.size(), op.second);
            pos = text.find(op.first, pos + op.second.size());
        }
    }
    return text;
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

std::string cpp_string_literal(std::string text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void emit_cpp_escape(std::ostringstream& out, const std::string& text, int depth) {
    std::istringstream body(cpp_escape_body(text));
    std::string line;
    while (std::getline(body, line)) {
        if (!trim_copy(line).empty()) {
            out << indent(depth) << trim_copy(line) << '\n';
        }
    }
}

void emit_source_comment(std::ostringstream& out, const RawStmt& stmt, int depth) {
    out << indent(depth) << "// dudu: " << format_location(stmt.location) << '\n';
}

void emit_simple_statement(std::ostringstream& out, const RawStmt& stmt, int depth,
                           const std::vector<std::string>& aliases,
                           std::map<std::string, std::string>& locals,
                           const std::string& return_type,
                           const std::map<std::string, std::string>& function_returns) {
    const std::string text = trim_copy(stmt.text);
    const size_t colon = find_top_level_colon(text);
    const size_t assign = find_top_level_assignment(text);
    if (starts_with(text, "cpp(")) {
        emit_cpp_escape(out, text, depth);
        return;
    }
    if (text == "pass") {
        out << indent(depth) << "(void)0;\n";
        return;
    }
    if (starts_with(text, "assert ")) {
        const std::string condition = trim_copy(text.substr(7));
        out << indent(depth) << "if (!(" << lower_expr(condition, aliases, locals)
            << ")) { throw std::runtime_error("
            << cpp_string_literal("assert failed: " + condition) << "); }\n";
        return;
    }
    if (starts_with(text, "return")) {
        const std::string value = trim_copy(text.substr(6));
        out << indent(depth) << "return";
        if (!value.empty()) {
            if (starts_with(return_type, "Option[") && value == "None") {
                out << " std::nullopt";
            } else if (split_top_level_args(value).size() > 1) {
                out << " {" << lower_expr(value, aliases, locals) << '}';
            } else {
                out << ' ' << lower_expr(value, aliases, locals);
            }
        }
        out << ";\n";
        return;
    }
    if (colon != std::string::npos && (assign == std::string::npos || colon < assign)) {
        const std::string name = trim_copy(text.substr(0, colon));
        const std::string type = trim_copy(text.substr(colon + 1, assign - colon - 1));
        locals[name] = type;
        out << indent(depth) << lower_cpp_type(type) << ' ' << name;
        if (assign != std::string::npos) {
            const std::string value = trim_copy(text.substr(assign + 1));
            if (starts_with(type, "Option[") && value == "None") {
                out << " = std::nullopt";
            } else if (starts_with(type, "list[") && value == "[]") {
                out << " = {}";
            } else if (starts_with(type, "list[") && starts_with(value, "[") &&
                       ends_with(value, "]")) {
                out << " = {" << lower_expr(value.substr(1, value.size() - 2), aliases, locals)
                    << '}';
            } else if (starts_with(type, "dict[") && starts_with(value, "{") &&
                       ends_with(value, "}")) {
                out << " = {" << lower_dict_literal_body(value, aliases, locals) << '}';
            } else if (starts_with(type, "set[") && starts_with(value, "{") &&
                       ends_with(value, "}")) {
                out << " = {" << lower_expr(value.substr(1, value.size() - 2), aliases, locals)
                    << '}';
            } else if (is_tuple_literal(value)) {
                out << " = " << lower_cpp_type(type) << "{"
                    << lower_expr(tuple_literal_body(value), aliases, locals) << '}';
            } else {
                out << " = " << lower_expr(value, aliases, locals);
            }
        } else {
            out << "{}";
        }
        out << ";\n";
        return;
    }
    if (assign != std::string::npos) {
        const std::string lhs = trim_copy(text.substr(0, assign));
        if (split_top_level_args(lhs).size() > 1) {
            out << indent(depth) << "auto [" << lhs
                << "] = " << lower_cpp_expr(trim_copy(text.substr(assign + 1)), aliases) << ";\n";
            return;
        }
        if (!lhs.empty() && lhs.find_first_of(" .[]+-*/%<>") == std::string::npos) {
            const std::string raw_value = trim_copy(text.substr(assign + 1));
            const std::string value = lower_expr(raw_value, aliases, locals);
            if (locals.contains(lhs)) {
                out << indent(depth) << lhs << " = ";
                if (starts_with(locals.at(lhs), "Option[") && raw_value == "None") {
                    out << "std::nullopt";
                } else {
                    out << value;
                }
                out << ";\n";
            } else {
                const std::string inferred =
                    infer_emitted_local_type(raw_value, locals, function_returns);
                locals.emplace(lhs, inferred.empty() ? "auto" : inferred);
                out << indent(depth) << "auto " << lhs << " = " << value << ";\n";
            }
            return;
        }
    }
    out << indent(depth) << lower_expr(normalize_spaced_compound(text), aliases, locals) << ";\n";
}

void emit_raw_statement(std::ostringstream& out, const RawStmt& stmt, int depth,
                        const std::vector<std::string>& aliases,
                        std::map<std::string, std::string>& locals, const std::string& return_type,
                        const std::map<std::string, std::string>& function_returns) {
    const std::string text = trim_copy(stmt.text);
    emit_source_comment(out, stmt, depth);
    if (starts_with(text, "if ")) {
        const std::string condition = strip_trailing_colon(text.substr(3));
        out << indent(depth) << if_keyword_for_condition(condition) << " ("
            << lower_expr(condition, aliases, locals) << ") {\n";
        emit_raw_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (starts_with(text, "elif ")) {
        const std::string condition = strip_trailing_colon(text.substr(5));
        out << indent(depth) << "else " << if_keyword_for_condition(condition) << " ("
            << lower_expr(condition, aliases, locals) << ") {\n";
        emit_raw_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (text == "else:") {
        out << indent(depth) << "else {\n";
        emit_raw_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (starts_with(text, "while ")) {
        out << indent(depth) << "while ("
            << lower_expr(strip_trailing_colon(text.substr(6)), aliases, locals) << ") {\n";
        emit_raw_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (starts_with(text, "for ")) {
        const std::string header = strip_trailing_colon(text.substr(4));
        const size_t in_pos = header.find(" in ");
        if (in_pos != std::string::npos) {
            std::string binding = trim_copy(header.substr(0, in_pos));
            const std::string range =
                lower_expr(trim_copy(header.substr(in_pos + 4)), aliases, locals);
            std::string binding_type = "auto";
            const size_t typed = binding.find(':');
            if (typed != std::string::npos) {
                binding_type = lower_cpp_type(trim_copy(binding.substr(typed + 1)));
                locals[trim_copy(binding.substr(0, typed))] = trim_copy(binding.substr(typed + 1));
                binding = trim_copy(binding.substr(0, typed));
            }
            if (starts_with(range, "range(") && ends_with(range, ")")) {
                const std::vector<std::string> args =
                    split_top_level_args(range.substr(6, range.size() - 7));
                const std::string start = args.size() == 1 ? "0" : args.at(0);
                const std::string end = args.size() == 1 ? args.at(0) : args.at(1);
                const std::string step = args.size() >= 3 ? args.at(2) : "1";
                out << indent(depth) << "for (" << binding_type << ' ' << binding << " = " << start
                    << "; " << binding << " < " << end << "; " << binding << " += " << step
                    << ") {\n";
                emit_raw_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                               function_returns);
                out << indent(depth) << "}\n";
                return;
            }
            const std::string loop_type = typed == std::string::npos ? "auto&&" : binding_type;
            out << indent(depth) << "for (" << loop_type << ' ' << binding << " : " << range
                << ") {\n";
            emit_raw_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                           function_returns);
            out << indent(depth) << "}\n";
            return;
        }
    }
    emit_simple_statement(out, stmt, depth, aliases, locals, return_type, function_returns);
}

} // namespace

void emit_raw_block(std::ostringstream& out, const std::vector<RawStmt>& body, int depth,
                    const std::vector<std::string>& aliases) {
    emit_raw_block(out, body, depth, aliases, {});
}

void emit_raw_block(std::ostringstream& out, const std::vector<RawStmt>& body, int depth,
                    const std::vector<std::string>& aliases,
                    const std::map<std::string, std::string>& initial_locals,
                    const std::string& return_type,
                    const std::map<std::string, std::string>& function_returns) {
    std::map<std::string, std::string> locals = initial_locals;
    for (const RawStmt& stmt : body) {
        emit_raw_statement(out, stmt, depth, aliases, locals, return_type, function_returns);
    }
}

} // namespace dudu
