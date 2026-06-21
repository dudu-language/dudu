#include "dudu/ast_parse_utils.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace dudu {
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
    if (!text.starts_with(keyword)) {
        return false;
    }
    if (text.size() == keyword.size()) {
        return true;
    }
    const char next = text[keyword.size()];
    return next == ':' || std::isspace(static_cast<unsigned char>(next)) != 0;
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

Expr make_expr(ExprKind kind, std::string_view text, SourceLocation location) {
    const size_t trim_start = text.find_first_not_of(" \t\r\n");
    if (trim_start != std::string_view::npos) {
        location = advance_columns(std::move(location), trim_start);
    }
    const std::string value = trim_string(text);
    Expr expr;
    expr.kind = kind;
    if (kind == ExprKind::Name) {
        expr.name = value;
    } else if (kind == ExprKind::BoolLiteral || kind == ExprKind::IntLiteral ||
               kind == ExprKind::FloatLiteral || kind == ExprKind::StringLiteral) {
        expr.value = value;
    }
    expr.location = location;
    expr.range = range_for_text(location, value);
    return expr;
}

TypeRef make_type(TypeKind kind, std::string_view text, SourceLocation location) {
    const size_t trim_start = text.find_first_not_of(" \t\r\n");
    if (trim_start != std::string_view::npos) {
        location = advance_columns(std::move(location), trim_start);
    }
    TypeRef type;
    type.kind = kind;
    const std::string value = trim_string(text);
    if (kind == TypeKind::Named || kind == TypeKind::Qualified || kind == TypeKind::Template) {
        type.name = value;
    } else if (kind == TypeKind::Value || kind == TypeKind::FixedArray) {
        type.value = value;
    }
    type.location = location;
    type.range = range_for_text(location, value);
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

} // namespace dudu
