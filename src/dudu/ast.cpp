#include "dudu/ast.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace dudu {
namespace {
std::string_view trim_view(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_identifier_continue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool starts_keyword(std::string_view text, std::string_view keyword) {
    text = trim_view(text);
    if (!text.starts_with(keyword)) {
        return false;
    }
    if (text.size() == keyword.size()) {
        return true;
    }
    return !is_identifier_continue(text[keyword.size()]);
}

bool starts_keyword_exact(std::string_view text, std::string_view keyword) {
    if (!text.starts_with(keyword)) {
        return false;
    }
    if (text.size() == keyword.size()) {
        return true;
    }
    return !is_identifier_continue(text[keyword.size()]);
}

bool starts_statement_keyword(std::string_view text, std::string_view keyword) {
    text = trim_view(text);
    return text.starts_with(keyword) && text.size() > keyword.size() &&
           std::isspace(static_cast<unsigned char>(text[keyword.size()])) != 0;
}

std::string trim_string(std::string_view text) {
    text = trim_view(text);
    return std::string(text);
}

SourceLocation advance_columns(SourceLocation location, size_t columns) {
    location.column += static_cast<int>(columns);
    return location;
}

std::string_view trim_view_with_location(std::string_view text, SourceLocation& location) {
    const size_t trim_start = text.find_first_not_of(" \t\r\n");
    if (trim_start == std::string_view::npos) {
        location = advance_columns(std::move(location), text.size());
        return {};
    }
    location = advance_columns(std::move(location), trim_start);
    text.remove_prefix(trim_start);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

SourceRange range_for_text(SourceLocation location, std::string_view text) {
    SourceRange range;
    range.start = location;
    range.end = advance_columns(location, text.size());
    return range;
}

SourceLocation location_for_piece(SourceLocation base, std::string_view full,
                                  std::string_view piece) {
    if (piece.empty()) {
        return base;
    }
    const size_t pos = full.find(piece);
    if (pos == std::string_view::npos) {
        return base;
    }
    return advance_columns(std::move(base), pos);
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

bool is_integer_literal(std::string_view text) {
    text = trim_view(text);
    if (text.empty()) {
        return false;
    }
    size_t pos = text.front() == '-' || text.front() == '+' ? 1 : 0;
    if (pos == text.size()) {
        return false;
    }
    auto valid_digit = [](char c, int base) {
        if (c == '_') {
            return true;
        }
        if (base <= 10) {
            return c >= '0' && c < static_cast<char>('0' + base);
        }
        return std::isdigit(static_cast<unsigned char>(c)) != 0 ||
               (c >= 'a' && c < static_cast<char>('a' + base - 10)) ||
               (c >= 'A' && c < static_cast<char>('A' + base - 10));
    };
    int base = 10;
    if (pos + 2 <= text.size() && text[pos] == '0' && pos + 1 < text.size()) {
        const char prefix = text[pos + 1];
        if (prefix == 'x' || prefix == 'X') {
            base = 16;
            pos += 2;
        } else if (prefix == 'b' || prefix == 'B') {
            base = 2;
            pos += 2;
        } else if (prefix == 'o' || prefix == 'O') {
            base = 8;
            pos += 2;
        }
    }
    bool saw_digit = false;
    for (; pos < text.size(); ++pos) {
        if (!valid_digit(text[pos], base)) {
            return false;
        }
        saw_digit = saw_digit || text[pos] != '_';
    }
    return saw_digit;
}

bool is_float_literal(std::string_view text) {
    text = trim_view(text);
    bool saw_dot = false;
    bool saw_digit = false;
    size_t pos = text.front() == '-' || text.front() == '+' ? 1 : 0;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            saw_digit = true;
            continue;
        }
        if (c == '_') {
            continue;
        }
        if (c == '.' && !saw_dot) {
            saw_dot = true;
            continue;
        }
        return false;
    }
    return saw_dot && saw_digit;
}

std::string strip_trailing_colon(std::string_view text) {
    text = trim_view(text);
    if (!text.empty() && text.back() == ':') {
        text.remove_suffix(1);
    }
    return trim_string(text);
}

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

struct CommaPart {
    std::string_view text;
    size_t offset = 0;
};

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
    for (size_t pos = text.size(); pos > 0; --pos) {
        const size_t i = pos - 1;
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
            if (std::isdigit(static_cast<unsigned char>(prev)) == 0 && is_identifier_start(next)) {
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
    for (size_t pos = text.size(); pos > 0; --pos) {
        const size_t i = pos - 1;
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

Expr make_expr(ExprKind kind, std::string_view text, SourceLocation location) {
    const size_t trim_start = text.find_first_not_of(" \t\r\n");
    if (trim_start != std::string_view::npos) {
        location = advance_columns(std::move(location), trim_start);
    }
    Expr expr;
    expr.kind = kind;
    expr.text = trim_string(text);
    expr.location = location;
    expr.range = range_for_text(location, expr.text);
    return expr;
}

std::vector<Expr> parse_expr_list(std::string_view text, SourceLocation location) {
    std::vector<Expr> out;
    for (const CommaPart& part : split_top_level_comma_parts(text)) {
        if (!part.text.empty()) {
            out.push_back(parse_expr_text(part.text, advance_columns(location, part.offset)));
        }
    }
    return out;
}

std::vector<Expr> parse_dict_entries(std::string_view text, SourceLocation location) {
    std::vector<Expr> out;
    for (const CommaPart& part : split_top_level_comma_parts(text)) {
        if (part.text.empty()) {
            continue;
        }
        Expr entry =
            make_expr(ExprKind::DictEntry, part.text, advance_columns(location, part.offset));
        const size_t colon = find_top_level_colon(part.text);
        if (colon == std::string_view::npos) {
            entry.children.push_back(parse_expr_text(part.text, entry.location));
        } else {
            entry.children.push_back(parse_expr_text(part.text.substr(0, colon), entry.location));
            entry.children.push_back(parse_expr_text(part.text.substr(colon + 1),
                                                     advance_columns(entry.location, colon + 1)));
        }
        out.push_back(std::move(entry));
    }
    return out;
}

TypeRef make_type(TypeKind kind, std::string_view text, SourceLocation location) {
    const size_t trim_start = text.find_first_not_of(" \t\r\n");
    if (trim_start != std::string_view::npos) {
        location = advance_columns(std::move(location), trim_start);
    }
    TypeRef type;
    type.kind = kind;
    type.text = trim_string(text);
    type.location = location;
    type.range = range_for_text(location, type.text);
    return type;
}

TypeKind wrapper_type_kind(std::string_view name) {
    if (name == "const") {
        return TypeKind::Const;
    }
    if (name == "volatile") {
        return TypeKind::Volatile;
    }
    if (name == "atomic") {
        return TypeKind::Atomic;
    }
    if (name == "device") {
        return TypeKind::Device;
    }
    if (name == "storage") {
        return TypeKind::Storage;
    }
    if (name == "shared") {
        return TypeKind::Shared;
    }
    if (name == "static") {
        return TypeKind::Static;
    }
    return TypeKind::Unknown;
}

std::vector<TypeRef> parse_type_list(std::string_view text, SourceLocation location) {
    std::vector<TypeRef> out;
    for (const std::string_view part : split_top_level_commas(text)) {
        if (!trim_view(part).empty()) {
            const size_t offset = static_cast<size_t>(part.data() - text.data());
            out.push_back(parse_type_text(part, advance_columns(location, offset)));
        }
    }
    return out;
}

size_t find_top_level_arrow(std::string_view text) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t i = 0; i + 1 < text.size(); ++i) {
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
                   text.substr(i, 2) == "->") {
            return i;
        }
    }
    return std::string_view::npos;
}

std::string compound_operator_before_assign(std::string_view text, size_t assign) {
    if (assign == std::string_view::npos) {
        return {};
    }
    size_t pos = assign;
    while (pos > 0 && std::isspace(static_cast<unsigned char>(text[pos - 1])) != 0) {
        --pos;
    }
    if (pos >= 2 && text.substr(pos - 2, 2) == "<<") {
        return "<<";
    }
    if (pos >= 2 && text.substr(pos - 2, 2) == ">>") {
        return ">>";
    }
    if (pos > 0) {
        return std::string(1, text[pos - 1]);
    }
    return {};
}

void fill_var_decl(Stmt& stmt, std::string_view text) {
    const size_t colon = find_top_level_colon_before_assign(text);
    if (colon == std::string_view::npos) {
        return;
    }
    stmt.name = trim_string(text.substr(0, colon));
    const size_t assign = find_top_level_assignment(text.substr(colon + 1), false);
    if (assign == std::string_view::npos) {
        stmt.type = trim_string(text.substr(colon + 1));
        return;
    }
    const size_t absolute_assign = colon + 1 + assign;
    stmt.type = trim_string(text.substr(colon + 1, assign));
    stmt.value = trim_string(text.substr(absolute_assign + 1));
}

void fill_assignment(Stmt& stmt, std::string_view text, bool compound) {
    const size_t assign = find_top_level_assignment(text, compound);
    if (assign == std::string_view::npos) {
        return;
    }
    if (compound) {
        stmt.op = compound_operator_before_assign(text, assign);
        size_t op_start = assign;
        while (op_start > 0 && std::isspace(static_cast<unsigned char>(text[op_start - 1])) != 0) {
            --op_start;
        }
        op_start -= stmt.op.size();
        stmt.target = trim_string(text.substr(0, op_start));
    } else {
        stmt.target = trim_string(text.substr(0, assign));
    }
    stmt.value = trim_string(text.substr(assign + 1));
}

void fill_condition(Stmt& stmt, std::string_view text, std::string_view keyword) {
    text = trim_view(text);
    stmt.condition = strip_trailing_colon(text.substr(keyword.size()));
}

void fill_for(Stmt& stmt, std::string_view text) {
    text = trim_view(text);
    std::string_view header = text.substr(3);
    header = trim_view(header);
    if (!header.empty() && header.back() == ':') {
        header.remove_suffix(1);
    }
    const size_t in_pos = find_top_level_word(header, "in");
    if (in_pos == std::string_view::npos) {
        stmt.condition = trim_string(header);
        return;
    }
    std::string_view binding = trim_view(header.substr(0, in_pos));
    const size_t colon = find_top_level_colon_before_assign(binding);
    if (colon == std::string_view::npos) {
        stmt.name = trim_string(binding);
    } else {
        stmt.name = trim_string(binding.substr(0, colon));
        stmt.type = trim_string(binding.substr(colon + 1));
    }
    stmt.iterable = strip_trailing_colon(header.substr(in_pos + 2));
}

void fill_case(Stmt& stmt, std::string_view text) {
    std::string header = strip_trailing_colon(trim_view(text).substr(4));
    const size_t guard_pos = find_top_level_word(header, "if");
    if (guard_pos == std::string_view::npos) {
        stmt.pattern = trim_string(header);
        return;
    }
    stmt.pattern = trim_string(std::string_view(header).substr(0, guard_pos));
    stmt.guard = trim_string(std::string_view(header).substr(guard_pos + 2));
}

void fill_except(Stmt& stmt, std::string_view text) {
    const std::string header = strip_trailing_colon(text.substr(6));
    if (header.empty()) {
        return;
    }
    const size_t colon = find_top_level_colon_before_assign(header);
    if (colon == std::string_view::npos) {
        stmt.condition = header;
        return;
    }
    stmt.name = trim_string(std::string_view(header).substr(0, colon));
    stmt.type = trim_string(std::string_view(header).substr(colon + 1));
}

void fill_assert(Stmt& stmt, std::string_view text, SourceLocation location,
                 std::string_view keyword) {
    const std::string_view body = trim_view(text.substr(keyword.size()));
    const std::vector<CommaPart> parts = split_top_level_comma_parts(body);
    if (parts.empty()) {
        return;
    }
    stmt.condition = trim_string(parts.front().text);
    stmt.condition_expr = parse_expr_text(
        stmt.condition, advance_columns(location, keyword.size() + parts.front().offset));
    if (parts.size() >= 2) {
        stmt.message = trim_string(parts[1].text);
        stmt.message_expr = parse_expr_text(
            stmt.message, advance_columns(location, keyword.size() + parts[1].offset));
    }
}
} // namespace

std::vector<std::string> tuple_binding_names(const Expr& expr) {
    if (expr.kind != ExprKind::TupleLiteral) {
        return {};
    }
    std::vector<std::string> names;
    names.reserve(expr.children.size());
    for (const Expr& child : expr.children) {
        if (child.kind != ExprKind::Name || child.name.empty()) {
            return {};
        }
        names.push_back(child.name);
    }
    return names;
}

std::string bound_import_name(const ImportDecl& import) {
    if (!import.alias.empty()) {
        return import.alias;
    }
    if (import.kind == ImportKind::From) {
        return import.imported_name;
    }
    const size_t dot = import.module_path.find('.');
    if (dot == std::string::npos) {
        return import.module_path;
    }
    return import.module_path.substr(0, dot);
}

std::string_view statement_kind_name(StmtKind kind) {
    switch (kind) {
    case StmtKind::Unknown:
        return "unknown";
    case StmtKind::Expr:
        return "expr";
    case StmtKind::VarDecl:
        return "var_decl";
    case StmtKind::Assign:
        return "assign";
    case StmtKind::CompoundAssign:
        return "compound_assign";
    case StmtKind::Return:
        return "return";
    case StmtKind::If:
        return "if";
    case StmtKind::Elif:
        return "elif";
    case StmtKind::Else:
        return "else";
    case StmtKind::Match:
        return "match";
    case StmtKind::Case:
        return "case";
    case StmtKind::While:
        return "while";
    case StmtKind::For:
        return "for";
    case StmtKind::Break:
        return "break";
    case StmtKind::Continue:
        return "continue";
    case StmtKind::Try:
        return "try";
    case StmtKind::Except:
        return "except";
    case StmtKind::Raise:
        return "raise";
    case StmtKind::Delete:
        return "delete";
    case StmtKind::Assert:
        return "assert";
    case StmtKind::DebugAssert:
        return "debug_assert";
    case StmtKind::CppEscape:
        return "cpp_escape";
    case StmtKind::Pass:
        return "pass";
    }
    return "unknown";
}

std::string_view expression_kind_name(ExprKind kind) {
    switch (kind) {
    case ExprKind::Unknown:
        return "unknown";
    case ExprKind::Name:
        return "name";
    case ExprKind::BoolLiteral:
        return "bool_literal";
    case ExprKind::IntLiteral:
        return "int_literal";
    case ExprKind::FloatLiteral:
        return "float_literal";
    case ExprKind::StringLiteral:
        return "string_literal";
    case ExprKind::NoneLiteral:
        return "none_literal";
    case ExprKind::Unary:
        return "unary";
    case ExprKind::Binary:
        return "binary";
    case ExprKind::Call:
        return "call";
    case ExprKind::TemplateCall:
        return "template_call";
    case ExprKind::Member:
        return "member";
    case ExprKind::Index:
        return "index";
    case ExprKind::ListLiteral:
        return "list_literal";
    case ExprKind::DictLiteral:
        return "dict_literal";
    case ExprKind::DictEntry:
        return "dict_entry";
    case ExprKind::NamedArg:
        return "named_arg";
    case ExprKind::Slice:
        return "slice";
    case ExprKind::SetLiteral:
        return "set_literal";
    case ExprKind::TupleLiteral:
        return "tuple_literal";
    case ExprKind::Lambda:
        return "lambda";
    case ExprKind::Conditional:
        return "conditional";
    case ExprKind::Await:
        return "await";
    case ExprKind::Yield:
        return "yield";
    case ExprKind::CppEscape:
        return "cpp_escape";
    }
    return "unknown";
}

std::string_view type_kind_name(TypeKind kind) {
    switch (kind) {
    case TypeKind::Unknown:
        return "unknown";
    case TypeKind::Named:
        return "named";
    case TypeKind::Qualified:
        return "qualified";
    case TypeKind::Value:
        return "value";
    case TypeKind::Template:
        return "template";
    case TypeKind::Pointer:
        return "pointer";
    case TypeKind::Reference:
        return "reference";
    case TypeKind::Const:
        return "const";
    case TypeKind::Volatile:
        return "volatile";
    case TypeKind::Atomic:
        return "atomic";
    case TypeKind::Device:
        return "device";
    case TypeKind::Storage:
        return "storage";
    case TypeKind::Shared:
        return "shared";
    case TypeKind::Static:
        return "static";
    case TypeKind::FixedArray:
        return "fixed_array";
    case TypeKind::Function:
        return "function";
    }
    return "unknown";
}

TypeRef parse_type_text(std::string_view text, SourceLocation location) {
    text = trim_view(text);
    if (text.empty()) {
        return make_type(TypeKind::Unknown, text, location);
    }
    const size_t arrow = find_top_level_arrow(text);
    if (arrow != std::string_view::npos) {
        TypeRef type = make_type(TypeKind::Function, text, location);
        type.children.push_back(
            parse_type_text(text.substr(arrow + 2), advance_columns(location, arrow + 2)));
        std::string_view params = trim_view(text.substr(0, arrow));
        size_t params_offset = 0;
        if (params.starts_with("fn(") && params.ends_with(")")) {
            params_offset = 3;
            params = params.substr(3, params.size() - 4);
        } else if (enclosed_by_outer_pair(params, '(', ')')) {
            params_offset = 1;
            params = params.substr(1, params.size() - 2);
        }
        std::vector<TypeRef> parsed_params =
            parse_type_list(params, advance_columns(location, params_offset));
        type.children.insert(type.children.end(), parsed_params.begin(), parsed_params.end());
        return type;
    }
    if (text.starts_with("fn(") && text.ends_with(")")) {
        TypeRef type = make_type(TypeKind::Function, text, location);
        type.children.push_back(parse_type_text("void", location));
        std::string_view params = text.substr(3, text.size() - 4);
        std::vector<TypeRef> parsed_params = parse_type_list(params, advance_columns(location, 3));
        type.children.insert(type.children.end(), parsed_params.begin(), parsed_params.end());
        return type;
    }
    if (text.front() == '*') {
        TypeRef type = make_type(TypeKind::Pointer, text, location);
        type.children.push_back(parse_type_text(text.substr(1), advance_columns(location, 1)));
        return type;
    }
    if (text.front() == '&') {
        TypeRef type = make_type(TypeKind::Reference, text, location);
        type.children.push_back(parse_type_text(text.substr(1), advance_columns(location, 1)));
        return type;
    }
    if (is_integer_literal(text)) {
        TypeRef type = make_type(TypeKind::Value, text, location);
        type.value = std::string(text);
        return type;
    }
    if (text.ends_with("]")) {
        const size_t open = find_matching_open(text, text.size() - 1, '[', ']');
        if (open != std::string_view::npos && open > 0) {
            const std::string_view head = trim_view(text.substr(0, open));
            const std::string_view inner = text.substr(open + 1, text.size() - open - 2);
            const TypeKind wrapper = wrapper_type_kind(head);
            if (wrapper != TypeKind::Unknown) {
                TypeRef type = make_type(wrapper, text, location);
                type.children.push_back(
                    parse_type_text(inner, advance_columns(location, open + 1)));
                return type;
            }
            if (head.ends_with("]")) {
                TypeRef type = make_type(TypeKind::FixedArray, text, location);
                type.children.push_back(parse_type_text(head, location));
                type.value = trim_string(inner);
                return type;
            }
            TypeRef type = make_type(TypeKind::Template, text, location);
            type.name = trim_string(head);
            type.children = parse_type_list(inner, advance_columns(location, open + 1));
            return type;
        }
    }
    if (text.find('.') != std::string_view::npos) {
        TypeRef type = make_type(TypeKind::Qualified, text, location);
        type.name = std::string(text);
        return type;
    }
    if (is_identifier(text)) {
        TypeRef type = make_type(TypeKind::Named, text, location);
        type.name = std::string(text);
        return type;
    }
    return make_type(TypeKind::Unknown, text, location);
}

Expr parse_expr_text(std::string_view text, SourceLocation location) {
    text = trim_view_with_location(text, location);
    if (text.empty()) {
        return make_expr(ExprKind::Unknown, text, location);
    }
    if (enclosed_by_outer_pair(text, '(', ')')) {
        const std::string_view inner = text.substr(1, text.size() - 2);
        if (split_top_level_commas(trim_view(inner)).size() > 1) {
            Expr expr = make_expr(ExprKind::TupleLiteral, text, location);
            expr.children = parse_expr_list(inner, advance_columns(location, 1));
            return expr;
        }
        return parse_expr_text(inner, advance_columns(location, 1));
    }
    if (starts_keyword(text, "lambda")) {
        Expr expr = make_expr(ExprKind::Lambda, text, location);
        size_t args_start = 6;
        while (args_start < text.size() &&
               std::isspace(static_cast<unsigned char>(text[args_start])) != 0) {
            ++args_start;
        }
        const std::string_view body = text.substr(args_start);
        const size_t colon = find_top_level_colon(body);
        if (colon != std::string_view::npos) {
            const std::string_view args = body.substr(0, colon);
            expr.name = trim_string(args);
            expr.params = parse_expr_list(args, advance_columns(location, args_start));
            expr.children.push_back(parse_expr_text(
                body.substr(colon + 1), advance_columns(location, args_start + colon + 1)));
        }
        return expr;
    }
    if (split_top_level_commas(text).size() > 1) {
        Expr expr = make_expr(ExprKind::TupleLiteral, text, location);
        expr.children = parse_expr_list(text, location);
        return expr;
    }
    if (enclosed_by_outer_pair(text, '[', ']')) {
        Expr expr = make_expr(ExprKind::ListLiteral, text, location);
        expr.children =
            parse_expr_list(text.substr(1, text.size() - 2), advance_columns(location, 1));
        return expr;
    }
    if (enclosed_by_outer_pair(text, '{', '}')) {
        const std::string_view body = text.substr(1, text.size() - 2);
        Expr expr =
            make_expr(has_top_level_colon(body) ? ExprKind::DictLiteral : ExprKind::SetLiteral,
                      text, location);
        expr.children = expr.kind == ExprKind::DictLiteral
                            ? parse_dict_entries(body, advance_columns(location, 1))
                            : parse_expr_list(body, advance_columns(location, 1));
        return expr;
    }
    if ((text.front() == '"' && text.back() == '"') ||
        (text.front() == '\'' && text.back() == '\'')) {
        Expr expr = make_expr(ExprKind::StringLiteral, text, location);
        expr.value = std::string(text.substr(1, text.size() - 2));
        return expr;
    }
    if (text == "True" || text == "False") {
        Expr expr = make_expr(ExprKind::BoolLiteral, text, location);
        expr.value = std::string(text);
        return expr;
    }
    if (text == "None") {
        return make_expr(ExprKind::NoneLiteral, text, location);
    }
    const size_t slice_colon = find_top_level_colon(text);
    if (slice_colon != std::string_view::npos) {
        Expr expr = make_expr(ExprKind::Slice, text, location);
        expr.children.push_back(parse_expr_text(text.substr(0, slice_colon), location));
        expr.children.push_back(parse_expr_text(text.substr(slice_colon + 1),
                                                advance_columns(location, slice_colon + 1)));
        return expr;
    }

    const size_t named_arg_assign = find_top_level_assignment(text, false);
    if (named_arg_assign != std::string_view::npos) {
        const std::string_view name = trim_view(text.substr(0, named_arg_assign));
        if (is_identifier(name)) {
            Expr expr = make_expr(ExprKind::NamedArg, text, location);
            expr.name = std::string(name);
            expr.children.push_back(
                parse_expr_text(text.substr(named_arg_assign + 1),
                                advance_columns(location, named_arg_assign + 1)));
            return expr;
        }
    }

    const size_t conditional_if = find_top_level_word(text, "if");
    if (conditional_if != std::string_view::npos) {
        const size_t conditional_else =
            find_top_level_word(text.substr(conditional_if + 2), "else");
        if (conditional_else != std::string_view::npos) {
            Expr expr = make_expr(ExprKind::Conditional, text, location);
            const size_t else_pos = conditional_if + 2 + conditional_else;
            expr.children.push_back(parse_expr_text(text.substr(0, conditional_if), location));
            expr.children.push_back(
                parse_expr_text(text.substr(conditional_if + 2, else_pos - conditional_if - 2),
                                advance_columns(location, conditional_if + 2)));
            expr.children.push_back(parse_expr_text(text.substr(else_pos + 4),
                                                    advance_columns(location, else_pos + 4)));
            return expr;
        }
    }

    const std::vector<std::vector<std::string_view>> precedence = {
        {"or"},       {"and"},    {"==", "!=", "<=", ">=", "<", ">"},
        {"|"},        {"^"},      {"&"},
        {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"},
    };
    for (const std::vector<std::string_view>& ops : precedence) {
        const size_t op_pos = find_top_level_binary_operator(text, ops);
        if (op_pos != std::string_view::npos) {
            std::string_view op;
            for (const std::string_view candidate : ops) {
                if (text.substr(op_pos, candidate.size()) == candidate) {
                    op = candidate;
                    break;
                }
            }
            Expr expr = make_expr(ExprKind::Binary, text, location);
            expr.op = std::string(op);
            expr.children.push_back(parse_expr_text(text.substr(0, op_pos), location));
            expr.children.push_back(parse_expr_text(text.substr(op_pos + op.size()),
                                                    advance_columns(location, op_pos + op.size())));
            return expr;
        }
    }

    if (starts_keyword(text, "not")) {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "not";
        expr.children.push_back(parse_expr_text(text.substr(3), advance_columns(location, 3)));
        return expr;
    }
    if (starts_keyword(text, "await")) {
        Expr expr = make_expr(ExprKind::Await, text, location);
        expr.children.push_back(parse_expr_text(text.substr(5), advance_columns(location, 5)));
        return expr;
    }
    if (starts_keyword(text, "yield")) {
        Expr expr = make_expr(ExprKind::Yield, text, location);
        expr.children.push_back(parse_expr_text(text.substr(5), advance_columns(location, 5)));
        return expr;
    }
    if (text.front() == '-' && !is_integer_literal(text) && !is_float_literal(text)) {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "-";
        expr.children.push_back(parse_expr_text(text.substr(1), advance_columns(location, 1)));
        return expr;
    }
    if (text.front() == '&') {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "&";
        expr.children.push_back(parse_expr_text(text.substr(1), advance_columns(location, 1)));
        return expr;
    }

    if (text.ends_with(")")) {
        const size_t open = find_matching_open(text, text.size() - 1, '(', ')');
        if (open != std::string_view::npos && open > 0) {
            std::string_view callee = trim_view(text.substr(0, open));
            Expr expr = make_expr(ExprKind::Call, text, location);
            expr.name = trim_string(callee);
            expr.callee.push_back(parse_expr_text(callee, location));
            expr.children = parse_expr_list(text.substr(open + 1, text.size() - open - 2),
                                            advance_columns(location, open + 1));
            if (callee.ends_with("]")) {
                const size_t type_open = find_matching_open(callee, callee.size() - 1, '[', ']');
                if (type_open != std::string_view::npos && type_open > 0) {
                    expr.kind = ExprKind::TemplateCall;
                    expr.name = trim_string(callee.substr(0, type_open));
                    expr.callee.clear();
                    expr.callee.push_back(parse_expr_text(callee.substr(0, type_open), location));
                    expr.template_args =
                        parse_expr_list(callee.substr(type_open + 1, callee.size() - type_open - 2),
                                        advance_columns(location, type_open + 1));
                    expr.template_type_args =
                        parse_type_list(callee.substr(type_open + 1, callee.size() - type_open - 2),
                                        advance_columns(location, type_open + 1));
                }
            }
            return expr;
        }
    }
    if (text.ends_with("]")) {
        const size_t open = find_matching_open(text, text.size() - 1, '[', ']');
        if (open != std::string_view::npos && open > 0) {
            Expr expr = make_expr(ExprKind::Index, text, location);
            expr.children.push_back(parse_expr_text(text.substr(0, open), location));
            expr.children.push_back(parse_expr_text(text.substr(open + 1, text.size() - open - 2),
                                                    advance_columns(location, open + 1)));
            return expr;
        }
    }
    const size_t dot = find_top_level_member_dot(text);
    if (dot != std::string_view::npos) {
        Expr expr = make_expr(ExprKind::Member, text, location);
        expr.name = trim_string(text.substr(dot + 1));
        expr.children.push_back(parse_expr_text(text.substr(0, dot), location));
        return expr;
    }
    if (starts_keyword(text, "cpp")) {
        return make_expr(ExprKind::CppEscape, text, location);
    }
    if (text.front() == '*') {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "*";
        expr.children.push_back(parse_expr_text(text.substr(1), advance_columns(location, 1)));
        return expr;
    }
    if (is_float_literal(text)) {
        Expr expr = make_expr(ExprKind::FloatLiteral, text, location);
        expr.value = std::string(text);
        return expr;
    }
    if (is_integer_literal(text)) {
        Expr expr = make_expr(ExprKind::IntLiteral, text, location);
        expr.value = std::string(text);
        return expr;
    }
    if (is_identifier(text)) {
        Expr expr = make_expr(ExprKind::Name, text, location);
        expr.name = std::string(text);
        return expr;
    }
    return make_expr(ExprKind::Unknown, text, location);
}

StmtKind classify_statement_text(std::string_view text) {
    text = trim_view(text);
    if (text.empty()) {
        return StmtKind::Unknown;
    }
    if (starts_keyword(text, "return")) {
        return StmtKind::Return;
    }
    if (starts_keyword(text, "if")) {
        return StmtKind::If;
    }
    if (starts_keyword(text, "elif")) {
        return StmtKind::Elif;
    }
    if (starts_keyword(text, "else")) {
        return StmtKind::Else;
    }
    if (starts_keyword(text, "match")) {
        return StmtKind::Match;
    }
    if (starts_keyword(text, "case")) {
        return StmtKind::Case;
    }
    if (starts_keyword(text, "while")) {
        return StmtKind::While;
    }
    if (starts_keyword(text, "for")) {
        return StmtKind::For;
    }
    if (starts_keyword(text, "break")) {
        return StmtKind::Break;
    }
    if (starts_keyword(text, "continue")) {
        return StmtKind::Continue;
    }
    if (starts_keyword(text, "try")) {
        return StmtKind::Try;
    }
    if (starts_keyword(text, "except")) {
        return StmtKind::Except;
    }
    if (starts_keyword(text, "raise")) {
        return StmtKind::Raise;
    }
    if (starts_keyword(text, "delete")) {
        return StmtKind::Delete;
    }
    if (starts_statement_keyword(text, "debug_assert")) {
        return StmtKind::DebugAssert;
    }
    if (starts_statement_keyword(text, "assert")) {
        return StmtKind::Assert;
    }
    if (starts_keyword(text, "cpp")) {
        return StmtKind::CppEscape;
    }
    if (starts_keyword(text, "pass")) {
        return StmtKind::Pass;
    }
    if (find_top_level_colon_before_assign(text) != std::string_view::npos) {
        return StmtKind::VarDecl;
    }
    if (find_top_level_assignment(text, true) != std::string_view::npos) {
        return StmtKind::CompoundAssign;
    }
    if (find_top_level_assignment(text, false) != std::string_view::npos) {
        return StmtKind::Assign;
    }
    return StmtKind::Expr;
}

Stmt statement_from_text(std::string raw_text, SourceLocation location, SourceRange range,
                         std::vector<Stmt> children) {
    Stmt stmt;
    stmt.kind = classify_statement_text(raw_text);
    stmt.text = std::move(raw_text);
    stmt.location = location;
    stmt.range =
        range.end.column <= range.start.column ? range_for_text(location, stmt.text) : range;
    const std::string_view text = trim_view(stmt.text);
    switch (stmt.kind) {
    case StmtKind::VarDecl:
        fill_var_decl(stmt, text);
        stmt.type_ref =
            parse_type_text(stmt.type, location_for_piece(location, stmt.text, stmt.type));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(location, stmt.text, stmt.value));
        break;
    case StmtKind::Assign:
        fill_assignment(stmt, text, false);
        stmt.target_expr =
            parse_expr_text(stmt.target, location_for_piece(location, stmt.text, stmt.target));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(location, stmt.text, stmt.value));
        break;
    case StmtKind::CompoundAssign:
        fill_assignment(stmt, text, true);
        stmt.target_expr =
            parse_expr_text(stmt.target, location_for_piece(location, stmt.text, stmt.target));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(location, stmt.text, stmt.value));
        break;
    case StmtKind::Return:
        stmt.value = trim_string(text.substr(6));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(location, stmt.text, stmt.value));
        break;
    case StmtKind::If:
        fill_condition(stmt, text, "if");
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(location, stmt.text, stmt.condition));
        break;
    case StmtKind::Elif:
        fill_condition(stmt, text, "elif");
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(location, stmt.text, stmt.condition));
        break;
    case StmtKind::Match:
        fill_condition(stmt, text, "match");
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(location, stmt.text, stmt.condition));
        break;
    case StmtKind::Case:
        fill_case(stmt, text);
        stmt.pattern_expr =
            parse_expr_text(stmt.pattern, location_for_piece(location, stmt.text, stmt.pattern));
        stmt.guard_expr =
            parse_expr_text(stmt.guard, location_for_piece(location, stmt.text, stmt.guard));
        break;
    case StmtKind::While:
        fill_condition(stmt, text, "while");
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(location, stmt.text, stmt.condition));
        break;
    case StmtKind::For:
        fill_for(stmt, text);
        stmt.type_ref =
            parse_type_text(stmt.type, location_for_piece(location, stmt.text, stmt.type));
        stmt.iterable_expr =
            parse_expr_text(stmt.iterable, location_for_piece(location, stmt.text, stmt.iterable));
        break;
    case StmtKind::Except:
        fill_except(stmt, text);
        stmt.type_ref =
            parse_type_text(stmt.type, location_for_piece(location, stmt.text, stmt.type));
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(location, stmt.text, stmt.condition));
        break;
    case StmtKind::Assert:
        fill_assert(stmt, text, location, "assert");
        break;
    case StmtKind::DebugAssert:
        fill_assert(stmt, text, location, "debug_assert");
        break;
    case StmtKind::Raise:
        stmt.value = trim_string(text.substr(5));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(location, stmt.text, stmt.value));
        break;
    case StmtKind::Delete:
        stmt.value = trim_string(text.substr(6));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(location, stmt.text, stmt.value));
        break;
    case StmtKind::Expr:
        stmt.expr = parse_expr_text(text, location);
        break;
    default:
        break;
    }
    stmt.children = std::move(children);
    return stmt;
}

} // namespace dudu
