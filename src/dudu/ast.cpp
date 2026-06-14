#include "dudu/ast.hpp"

#include <algorithm>
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

bool has_top_level_colon_before_assign(std::string_view text) {
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
                return true;
            }
            if (c == '=') {
                return false;
            }
        }
    }
    return false;
}

bool has_top_level_assignment(std::string_view text, bool compound) {
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
                return true;
            }
        }
    }
    return false;
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
    if (starts_keyword(text, "debug_assert")) {
        return StmtKind::DebugAssert;
    }
    if (starts_keyword(text, "assert")) {
        return StmtKind::Assert;
    }
    if (starts_keyword(text, "cpp")) {
        return StmtKind::CppEscape;
    }
    if (starts_keyword(text, "pass")) {
        return StmtKind::Pass;
    }
    if (has_top_level_colon_before_assign(text)) {
        return StmtKind::VarDecl;
    }
    if (has_top_level_assignment(text, true)) {
        return StmtKind::CompoundAssign;
    }
    if (has_top_level_assignment(text, false)) {
        return StmtKind::Assign;
    }
    return StmtKind::Expr;
}

Stmt statement_from_raw(const RawStmt& raw) {
    Stmt stmt;
    stmt.kind = classify_statement_text(raw.text);
    stmt.text = raw.text;
    stmt.location = raw.location;
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
