#include "dudu/ast_parse_utils.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace dudu {
namespace {
struct UnsupportedStatement {
    std::string_view keyword;
    std::string_view feature;
};

constexpr UnsupportedStatement unsupported_statements[] = {
    {"finally", "exceptions"},
    {"yield", "generators"},
    {"async", "async"},
    {"await", "async"},
    {"with", "context managers"},
    {"global", "global rebinding"},
    {"nonlocal", "nonlocal rebinding"},
    {"del", "dynamic deletion"},
    {"def", "local function declarations"},
    {"import", "local imports"},
    {"from", "local imports"},
};

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

SourceLocation statement_piece_location(const Stmt& stmt, std::string_view piece) {
    if (!piece.empty() && stmt.source_text.find(piece) != std::string::npos) {
        return location_for_piece(stmt.location, stmt.source_text, piece);
    }
    return location_for_piece(stmt.location, stmt.text, piece);
}

std::string_view unsupported_statement_feature(std::string_view text) {
    text = trim_view(text);
    for (const UnsupportedStatement& unsupported : unsupported_statements) {
        if (starts_statement_keyword(text, unsupported.keyword)) {
            return unsupported.feature;
        }
    }
    return {};
}

} // namespace

StmtKind classify_statement_text(std::string_view text) {
    text = trim_view(text);
    if (text.empty()) {
        return StmtKind::Unknown;
    }
    if (!unsupported_statement_feature(text).empty()) {
        return StmtKind::Unsupported;
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

Stmt statement_from_text(std::string raw_text, std::string source_text, SourceLocation location,
                         SourceRange range, std::vector<Stmt> children) {
    Stmt stmt;
    stmt.kind = classify_statement_text(raw_text);
    stmt.text = std::move(raw_text);
    stmt.source_text = source_text.empty() ? stmt.text : std::move(source_text);
    stmt.location = location;
    stmt.range =
        range.end.column <= range.start.column ? range_for_text(location, stmt.text) : range;
    const std::string_view text = trim_view(stmt.text);
    switch (stmt.kind) {
    case StmtKind::Unsupported:
        stmt.unsupported_feature = std::string(unsupported_statement_feature(text));
        break;
    case StmtKind::VarDecl:
        fill_var_decl(stmt, text);
        stmt.type_ref = parse_type_text(stmt.type, statement_piece_location(stmt, stmt.type));
        stmt.value_expr = parse_expr_text(stmt.value, statement_piece_location(stmt, stmt.value));
        break;
    case StmtKind::Assign:
        fill_assignment(stmt, text, false);
        stmt.target_expr =
            parse_expr_text(stmt.target, statement_piece_location(stmt, stmt.target));
        stmt.value_expr = parse_expr_text(stmt.value, statement_piece_location(stmt, stmt.value));
        break;
    case StmtKind::CompoundAssign:
        fill_assignment(stmt, text, true);
        stmt.target_expr =
            parse_expr_text(stmt.target, statement_piece_location(stmt, stmt.target));
        stmt.value_expr = parse_expr_text(stmt.value, statement_piece_location(stmt, stmt.value));
        break;
    case StmtKind::Return:
        stmt.value = trim_string(text.substr(6));
        stmt.value_expr = parse_expr_text(stmt.value, statement_piece_location(stmt, stmt.value));
        break;
    case StmtKind::If:
        fill_condition(stmt, text, "if");
        stmt.condition_expr =
            parse_expr_text(stmt.condition, statement_piece_location(stmt, stmt.condition));
        break;
    case StmtKind::Elif:
        fill_condition(stmt, text, "elif");
        stmt.condition_expr =
            parse_expr_text(stmt.condition, statement_piece_location(stmt, stmt.condition));
        break;
    case StmtKind::Match:
        fill_condition(stmt, text, "match");
        stmt.condition_expr =
            parse_expr_text(stmt.condition, statement_piece_location(stmt, stmt.condition));
        break;
    case StmtKind::Case:
        fill_case(stmt, text);
        stmt.pattern_expr =
            parse_expr_text(stmt.pattern, statement_piece_location(stmt, stmt.pattern));
        stmt.guard_expr = parse_expr_text(stmt.guard, statement_piece_location(stmt, stmt.guard));
        break;
    case StmtKind::While:
        fill_condition(stmt, text, "while");
        stmt.condition_expr =
            parse_expr_text(stmt.condition, statement_piece_location(stmt, stmt.condition));
        break;
    case StmtKind::For:
        fill_for(stmt, text);
        stmt.type_ref = parse_type_text(stmt.type, statement_piece_location(stmt, stmt.type));
        stmt.iterable_expr =
            parse_expr_text(stmt.iterable, statement_piece_location(stmt, stmt.iterable));
        break;
    case StmtKind::Except:
        fill_except(stmt, text);
        stmt.type_ref = parse_type_text(stmt.type, statement_piece_location(stmt, stmt.type));
        stmt.condition_expr =
            parse_expr_text(stmt.condition, statement_piece_location(stmt, stmt.condition));
        break;
    case StmtKind::Assert:
        fill_assert(stmt, text, location, "assert");
        break;
    case StmtKind::DebugAssert:
        fill_assert(stmt, text, location, "debug_assert");
        break;
    case StmtKind::Raise:
        stmt.value = trim_string(text.substr(5));
        stmt.value_expr = parse_expr_text(stmt.value, statement_piece_location(stmt, stmt.value));
        break;
    case StmtKind::Delete:
        stmt.value = trim_string(text.substr(6));
        stmt.value_expr = parse_expr_text(stmt.value, statement_piece_location(stmt, stmt.value));
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
