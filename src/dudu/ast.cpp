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
    for (; pos < text.size(); ++pos) {
        if (std::isdigit(static_cast<unsigned char>(text[pos])) == 0) {
            return false;
        }
    }
    return true;
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

std::vector<std::string_view> split_top_level_commas(std::string_view text) {
    std::vector<std::string_view> parts;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    size_t start = 0;
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
        } else if (bracket_depth == 0 && paren_depth == 0 && brace_depth == 0 && c == ',') {
            parts.push_back(trim_view(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim_view(text.substr(start)));
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
            if ((op == "and" || op == "or") && !((i == 0 || !is_identifier_continue(text[i - 1])) &&
                                                 starts_keyword_exact(text.substr(i), op))) {
                continue;
            }
            if ((op == "+" || op == "-") &&
                (i == 0 || text[i - 1] == '(' || text[i - 1] == '[' || text[i - 1] == ',')) {
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
    for (const std::string_view part : split_top_level_commas(text)) {
        if (!trim_view(part).empty()) {
            out.push_back(parse_expr_text(part, location));
        }
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
            out.push_back(parse_type_text(part, location));
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
} // namespace

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
    case ExprKind::SetLiteral:
        return "set_literal";
    case ExprKind::TupleLiteral:
        return "tuple_literal";
    case ExprKind::Lambda:
        return "lambda";
    case ExprKind::Conditional:
        return "conditional";
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
        type.children.push_back(parse_type_text(text.substr(arrow + 2), location));
        std::string_view params = trim_view(text.substr(0, arrow));
        if (enclosed_by_outer_pair(params, '(', ')')) {
            params = params.substr(1, params.size() - 2);
        }
        std::vector<TypeRef> parsed_params = parse_type_list(params, location);
        type.children.insert(type.children.end(), parsed_params.begin(), parsed_params.end());
        return type;
    }
    if (text.front() == '*') {
        TypeRef type = make_type(TypeKind::Pointer, text, location);
        type.children.push_back(parse_type_text(text.substr(1), location));
        return type;
    }
    if (text.front() == '&') {
        TypeRef type = make_type(TypeKind::Reference, text, location);
        type.children.push_back(parse_type_text(text.substr(1), location));
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
                type.children.push_back(parse_type_text(inner, location));
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
            type.children = parse_type_list(inner, location);
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
    text = trim_view(text);
    if (text.empty()) {
        return make_expr(ExprKind::Unknown, text, location);
    }
    if (enclosed_by_outer_pair(text, '(', ')')) {
        const std::string_view inner = trim_view(text.substr(1, text.size() - 2));
        if (split_top_level_commas(inner).size() > 1) {
            Expr expr = make_expr(ExprKind::TupleLiteral, text, location);
            expr.children = parse_expr_list(inner, location);
            return expr;
        }
        return parse_expr_text(inner, location);
    }
    if (enclosed_by_outer_pair(text, '[', ']')) {
        Expr expr = make_expr(ExprKind::ListLiteral, text, location);
        expr.children = parse_expr_list(text.substr(1, text.size() - 2), location);
        return expr;
    }
    if (enclosed_by_outer_pair(text, '{', '}')) {
        Expr expr =
            make_expr(has_top_level_colon(text.substr(1, text.size() - 2)) ? ExprKind::DictLiteral
                                                                           : ExprKind::SetLiteral,
                      text, location);
        expr.children = parse_expr_list(text.substr(1, text.size() - 2), location);
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
    if (starts_keyword(text, "lambda")) {
        return make_expr(ExprKind::Lambda, text, location);
    }

    const size_t conditional_if = find_top_level_word(text, "if");
    if (conditional_if != std::string_view::npos) {
        const size_t conditional_else =
            find_top_level_word(text.substr(conditional_if + 2), "else");
        if (conditional_else != std::string_view::npos) {
            Expr expr = make_expr(ExprKind::Conditional, text, location);
            const size_t else_pos = conditional_if + 2 + conditional_else;
            expr.children.push_back(parse_expr_text(text.substr(0, conditional_if), location));
            expr.children.push_back(parse_expr_text(
                text.substr(conditional_if + 2, else_pos - conditional_if - 2), location));
            expr.children.push_back(parse_expr_text(text.substr(else_pos + 4), location));
            return expr;
        }
    }

    const std::vector<std::vector<std::string_view>> precedence = {
        {"or"}, {"and"}, {"==", "!=", "<=", ">=", "<", ">"}, {"+", "-"}, {"*", "/", "%"},
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
            expr.children.push_back(parse_expr_text(text.substr(op_pos + op.size()), location));
            return expr;
        }
    }

    if (starts_keyword(text, "not")) {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "not";
        expr.children.push_back(parse_expr_text(text.substr(3), location));
        return expr;
    }
    if (text.front() == '-' && !is_integer_literal(text) && !is_float_literal(text)) {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "-";
        expr.children.push_back(parse_expr_text(text.substr(1), location));
        return expr;
    }

    if (text.ends_with(")")) {
        const size_t open = find_matching_open(text, text.size() - 1, '(', ')');
        if (open != std::string_view::npos && open > 0) {
            std::string_view callee = trim_view(text.substr(0, open));
            Expr expr = make_expr(ExprKind::Call, text, location);
            expr.name = trim_string(callee);
            expr.children =
                parse_expr_list(text.substr(open + 1, text.size() - open - 2), location);
            if (callee.ends_with("]")) {
                const size_t type_open = find_matching_open(callee, callee.size() - 1, '[', ']');
                if (type_open != std::string_view::npos && type_open > 0) {
                    expr.kind = ExprKind::TemplateCall;
                    expr.name = trim_string(callee.substr(0, type_open));
                    std::vector<Expr> template_args = parse_expr_list(
                        callee.substr(type_open + 1, callee.size() - type_open - 2), location);
                    template_args.insert(template_args.end(), expr.children.begin(),
                                         expr.children.end());
                    expr.children = std::move(template_args);
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
            expr.children.push_back(
                parse_expr_text(text.substr(open + 1, text.size() - open - 2), location));
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

Stmt statement_from_raw(const RawStmt& raw) {
    Stmt stmt;
    stmt.kind = classify_statement_text(raw.text);
    stmt.text = raw.text;
    stmt.location = raw.location;
    stmt.range = raw.range.end.column <= raw.range.start.column
                     ? range_for_text(raw.location, raw.text)
                     : raw.range;
    const std::string_view text = trim_view(raw.text);
    switch (stmt.kind) {
    case StmtKind::VarDecl:
        fill_var_decl(stmt, text);
        stmt.type_ref =
            parse_type_text(stmt.type, location_for_piece(raw.location, raw.text, stmt.type));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(raw.location, raw.text, stmt.value));
        break;
    case StmtKind::Assign:
        fill_assignment(stmt, text, false);
        stmt.target_expr =
            parse_expr_text(stmt.target, location_for_piece(raw.location, raw.text, stmt.target));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(raw.location, raw.text, stmt.value));
        break;
    case StmtKind::CompoundAssign:
        fill_assignment(stmt, text, true);
        stmt.target_expr =
            parse_expr_text(stmt.target, location_for_piece(raw.location, raw.text, stmt.target));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(raw.location, raw.text, stmt.value));
        break;
    case StmtKind::Return:
        stmt.value = trim_string(text.substr(6));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(raw.location, raw.text, stmt.value));
        break;
    case StmtKind::If:
        fill_condition(stmt, text, "if");
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(raw.location, raw.text, stmt.condition));
        break;
    case StmtKind::Elif:
        fill_condition(stmt, text, "elif");
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(raw.location, raw.text, stmt.condition));
        break;
    case StmtKind::While:
        fill_condition(stmt, text, "while");
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(raw.location, raw.text, stmt.condition));
        break;
    case StmtKind::For:
        fill_for(stmt, text);
        stmt.type_ref =
            parse_type_text(stmt.type, location_for_piece(raw.location, raw.text, stmt.type));
        stmt.iterable_expr = parse_expr_text(
            stmt.iterable, location_for_piece(raw.location, raw.text, stmt.iterable));
        break;
    case StmtKind::Assert:
        stmt.condition = trim_string(text.substr(6));
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(raw.location, raw.text, stmt.condition));
        break;
    case StmtKind::DebugAssert:
        stmt.condition = trim_string(text.substr(12));
        stmt.condition_expr = parse_expr_text(
            stmt.condition, location_for_piece(raw.location, raw.text, stmt.condition));
        break;
    case StmtKind::Raise:
        stmt.value = trim_string(text.substr(5));
        stmt.value_expr =
            parse_expr_text(stmt.value, location_for_piece(raw.location, raw.text, stmt.value));
        break;
    case StmtKind::Expr:
        stmt.expr = parse_expr_text(text, raw.location);
        break;
    default:
        break;
    }
    stmt.children = statements_from_raw(raw.children);
    return stmt;
}

std::vector<Stmt> statements_from_raw(const std::vector<RawStmt>& raw) {
    std::vector<Stmt> out;
    out.reserve(raw.size());
    for (const RawStmt& stmt : raw) {
        out.push_back(statement_from_raw(stmt));
    }
    return out;
}

} // namespace dudu
