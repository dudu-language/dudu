#include "dudu/ast.hpp"

#include <cctype>

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
        while (op_start > 0 &&
               std::isspace(static_cast<unsigned char>(text[op_start - 1])) != 0) {
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
    const std::string_view text = trim_view(raw.text);
    switch (stmt.kind) {
    case StmtKind::VarDecl:
        fill_var_decl(stmt, text);
        break;
    case StmtKind::Assign:
        fill_assignment(stmt, text, false);
        break;
    case StmtKind::CompoundAssign:
        fill_assignment(stmt, text, true);
        break;
    case StmtKind::Return:
        stmt.value = trim_string(text.substr(6));
        break;
    case StmtKind::If:
        fill_condition(stmt, text, "if");
        break;
    case StmtKind::Elif:
        fill_condition(stmt, text, "elif");
        break;
    case StmtKind::While:
        fill_condition(stmt, text, "while");
        break;
    case StmtKind::For:
        fill_for(stmt, text);
        break;
    case StmtKind::Assert:
        stmt.condition = trim_string(text.substr(6));
        break;
    case StmtKind::DebugAssert:
        stmt.condition = trim_string(text.substr(12));
        break;
    case StmtKind::Raise:
        stmt.value = trim_string(text.substr(5));
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
