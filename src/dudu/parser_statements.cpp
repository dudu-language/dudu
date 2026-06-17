#include "dudu/ast_parse_utils.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/parser_internal.hpp"

#include <optional>
#include <string_view>

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

bool token_is_identifier(const Token& token, std::string_view text) {
    return token.kind == TokenKind::Identifier && token.text == text;
}

bool token_is_stop(const Token& token, std::initializer_list<TokenKind> stops) {
    for (const TokenKind stop : stops) {
        if (token.kind == stop) {
            return true;
        }
    }
    return false;
}

bool has_statement_tokens_after(const std::span<const Token> tokens, size_t cursor) {
    for (size_t index = cursor; index < tokens.size(); ++index) {
        if (tokens[index].kind == TokenKind::Newline || tokens[index].kind == TokenKind::End) {
            return false;
        }
        if (tokens[index].kind != TokenKind::Colon) {
            return true;
        }
    }
    return false;
}

bool is_assignment_operator(const Token& token) {
    if (token.kind == TokenKind::Assign) {
        return true;
    }
    return token.kind == TokenKind::Operator && token.text.size() >= 2 &&
           token.text.back() == '=' && token.text != "==" && token.text != "!=" &&
           token.text != "<=" && token.text != ">=";
}

bool is_compound_assignment_operator(const Token& token) {
    return token.kind == TokenKind::Operator && is_assignment_operator(token);
}

std::optional<size_t> find_top_level_token(std::span<const Token> tokens, size_t begin, size_t end,
                                           TokenKind kind) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t index = begin; index < end; ++index) {
        const Token& token = tokens[index];
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group && token.kind == kind) {
            return index;
        }
        if (token.kind == TokenKind::LBracket) {
            ++bracket_depth;
        } else if (token.kind == TokenKind::RBracket) {
            --bracket_depth;
        } else if (token.kind == TokenKind::LParen) {
            ++paren_depth;
        } else if (token.kind == TokenKind::RParen) {
            --paren_depth;
        } else if (token.kind == TokenKind::LBrace) {
            ++brace_depth;
        } else if (token.kind == TokenKind::RBrace) {
            --brace_depth;
        }
    }
    return std::nullopt;
}

std::optional<size_t> find_top_level_assignment_token(std::span<const Token> tokens, size_t begin,
                                                      size_t end) {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t index = begin; index < end; ++index) {
        const Token& token = tokens[index];
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group && is_assignment_operator(token)) {
            return index;
        }
        if (token.kind == TokenKind::LBracket) {
            ++bracket_depth;
        } else if (token.kind == TokenKind::RBracket) {
            --bracket_depth;
        } else if (token.kind == TokenKind::LParen) {
            ++paren_depth;
        } else if (token.kind == TokenKind::RParen) {
            --paren_depth;
        } else if (token.kind == TokenKind::LBrace) {
            ++brace_depth;
        } else if (token.kind == TokenKind::RBrace) {
            --brace_depth;
        }
    }
    return std::nullopt;
}

std::string compound_assignment_op(std::string_view token_text) {
    std::string op(token_text);
    if (!op.empty()) {
        op.pop_back();
    }
    return op;
}

std::string_view unsupported_feature_for_token(const Token& token) {
    if (token.kind != TokenKind::Identifier) {
        return {};
    }
    for (const UnsupportedStatement& unsupported : unsupported_statements) {
        if (token.text == unsupported.keyword) {
            return unsupported.feature;
        }
    }
    return {};
}

void attach_statement_source(Stmt& stmt, const Parser::JoinedTokens& joined) {
    stmt.source_text = joined.source_text.empty() ? joined.text : joined.source_text;
    if (joined.has_tokens) {
        stmt.range = joined.range;
    } else {
        stmt.range = range_for_text(stmt.location, stmt.source_text);
    }
}

void parse_assert_parts(Stmt& stmt, const Parser::JoinedTokens& body) {
    const std::vector<CommaPart> parts = split_top_level_comma_parts(body.text);
    if (parts.empty()) {
        return;
    }
    stmt.condition_expr = parse_expr_text(trim_string(parts.front().text),
                                          advance_columns(body.range.start, parts.front().offset));
    if (parts.size() >= 2) {
        stmt.message_expr = parse_expr_text(trim_string(parts[1].text),
                                            advance_columns(body.range.start, parts[1].offset));
    }
}

void fill_expr_piece(Expr& expr, const Parser::JoinedTokens& piece) {
    expr = parse_expr_text(piece.text, piece.range.start);
}

void fill_type_piece(TypeRef& type_ref, const Parser::JoinedTokens& piece) {
    type_ref = parse_type_text(piece.text, piece.range.start);
}

} // namespace

Parser::JoinedTokens
Parser::join_until_top_level_identifier(std::string_view identifier,
                                        std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    while (!at(TokenKind::End)) {
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group &&
            (token_is_identifier(current(), identifier) || token_is_stop(current(), stops))) {
            break;
        }
        if (current().kind == TokenKind::LBracket) {
            ++bracket_depth;
        } else if (current().kind == TokenKind::RBracket) {
            --bracket_depth;
        } else if (current().kind == TokenKind::LParen) {
            ++paren_depth;
        } else if (current().kind == TokenKind::RParen) {
            --paren_depth;
        } else if (current().kind == TokenKind::LBrace) {
            ++brace_depth;
        } else if (current().kind == TokenKind::RBrace) {
            --brace_depth;
        }
        ++cursor_;
    }
    return join_tokens(begin, cursor_);
}

Stmt Parser::parse_statement(std::vector<Stmt> children, size_t statement_end) {
    const size_t begin = cursor_;
    const Token& start = current();
    Stmt stmt;
    stmt.location = start.location;
    stmt.children = std::move(children);

    if (const std::string_view unsupported = unsupported_feature_for_token(start);
        !unsupported.empty()) {
        stmt.kind = StmtKind::Unsupported;
        stmt.unsupported_feature = std::string(unsupported);
        join_until_with_range({TokenKind::Newline});
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("return")) {
        stmt.kind = StmtKind::Return;
        const JoinedTokens value = join_until_with_range({TokenKind::Newline});
        stmt.value_expr = parse_expr_text(value.text, value.range.start);
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("if") || match_identifier("elif") || match_identifier("while") ||
        match_identifier("match")) {
        const std::string keyword = previous().text;
        stmt.kind = keyword == "if"      ? StmtKind::If
                    : keyword == "elif"  ? StmtKind::Elif
                    : keyword == "while" ? StmtKind::While
                                         : StmtKind::Match;
        const JoinedTokens condition =
            join_until_with_range({TokenKind::Colon, TokenKind::Newline});
        stmt.condition_expr = parse_expr_text(condition.text, condition.range.start);
        consume(TokenKind::Colon, "expected : after " + keyword + " condition");
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("else") || match_identifier("try")) {
        const std::string keyword = previous().text;
        stmt.kind = keyword == "else" ? StmtKind::Else : StmtKind::Try;
        consume(TokenKind::Colon, "expected : after " + keyword);
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("for")) {
        stmt.kind = StmtKind::For;
        const Token& name = consume_identifier("expected for loop binding");
        stmt.name = name.text;
        if (match(TokenKind::Colon)) {
            const JoinedTokens type = join_until_top_level_identifier("in", {TokenKind::Newline});
            stmt.type = type.text;
            stmt.type_ref = parse_type_text(stmt.type, type.range.start);
        }
        if (!match_identifier("in")) {
            fail_current("expected in after for loop binding");
        }
        const JoinedTokens iterable = join_until_with_range({TokenKind::Colon, TokenKind::Newline});
        stmt.iterable_expr = parse_expr_text(iterable.text, iterable.range.start);
        consume(TokenKind::Colon, "expected : after for loop iterable");
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("case")) {
        stmt.kind = StmtKind::Case;
        const JoinedTokens pattern = join_until_top_level_identifier("if", {TokenKind::Colon});
        stmt.pattern_expr = parse_expr_text(pattern.text, pattern.range.start);
        if (match_identifier("if")) {
            const JoinedTokens guard =
                join_until_with_range({TokenKind::Colon, TokenKind::Newline});
            stmt.guard_expr = parse_expr_text(guard.text, guard.range.start);
        }
        consume(TokenKind::Colon, "expected : after case pattern");
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("except")) {
        stmt.kind = StmtKind::Except;
        if (at(TokenKind::Colon)) {
            consume(TokenKind::Colon, "expected : after except");
            attach_statement_source(stmt, join_tokens(begin, cursor_));
            return stmt;
        }
        const JoinedTokens header = join_until_with_range({TokenKind::Colon, TokenKind::Newline});
        const bool binding_header =
            at(TokenKind::Colon) && has_statement_tokens_after(tokens_, cursor_ + 1);
        if (binding_header) {
            stmt.name = header.text;
            consume(TokenKind::Colon, "expected : after except binding");
            const JoinedTokens type = join_until_with_range({TokenKind::Colon, TokenKind::Newline});
            stmt.type = type.text;
            stmt.type_ref = parse_type_text(stmt.type, type.range.start);
        } else {
            stmt.condition_expr = parse_expr_text(header.text, header.range.start);
        }
        consume(TokenKind::Colon, "expected : after except");
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("break") || match_identifier("continue") || match_identifier("pass")) {
        const std::string keyword = previous().text;
        stmt.kind = keyword == "break"      ? StmtKind::Break
                    : keyword == "continue" ? StmtKind::Continue
                                            : StmtKind::Pass;
        join_until_with_range({TokenKind::Newline});
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("raise") || match_identifier("delete")) {
        const std::string keyword = previous().text;
        stmt.kind = keyword == "raise" ? StmtKind::Raise : StmtKind::Delete;
        const JoinedTokens value = join_until_with_range({TokenKind::Newline});
        stmt.value_expr = parse_expr_text(value.text, value.range.start);
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("assert") || match_identifier("debug_assert")) {
        const std::string keyword = previous().text;
        stmt.kind = keyword == "assert" ? StmtKind::Assert : StmtKind::DebugAssert;
        parse_assert_parts(stmt, join_until_with_range({TokenKind::Newline}));
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (check_text("cpp")) {
        stmt.kind = StmtKind::CppEscape;
        join_until_with_range({TokenKind::Newline});
        const JoinedTokens source = join_tokens(begin, cursor_);
        stmt.cpp_body = cpp_escape_body(source.text);
        attach_statement_source(stmt, source);
        return stmt;
    }

    const size_t line_begin = cursor_;
    cursor_ = statement_end;
    const size_t end = cursor_;
    const JoinedTokens whole = join_tokens(line_begin, end);
    attach_statement_source(stmt, whole);

    const std::optional<size_t> colon =
        find_top_level_token(tokens_, line_begin, end, TokenKind::Colon);
    const std::optional<size_t> assignment =
        find_top_level_assignment_token(tokens_, line_begin, end);
    if (colon.has_value() && (!assignment.has_value() || *colon < *assignment)) {
        stmt.kind = StmtKind::VarDecl;
        const JoinedTokens name = join_tokens(line_begin, *colon);
        stmt.name = name.text;
        const size_t type_end = assignment.value_or(end);
        const JoinedTokens type = join_tokens(*colon + 1, type_end);
        stmt.type = type.text;
        fill_type_piece(stmt.type_ref, type);
        if (assignment.has_value()) {
            const JoinedTokens value = join_tokens(*assignment + 1, end);
            fill_expr_piece(stmt.value_expr, value);
        }
        return stmt;
    }

    if (assignment.has_value()) {
        const Token& assign = tokens_[*assignment];
        stmt.kind =
            is_compound_assignment_operator(assign) ? StmtKind::CompoundAssign : StmtKind::Assign;
        if (stmt.kind == StmtKind::CompoundAssign) {
            stmt.op = compound_assignment_op(assign.text);
        }
        const JoinedTokens target = join_tokens(line_begin, *assignment);
        fill_expr_piece(stmt.target_expr, target);
        const JoinedTokens value = join_tokens(*assignment + 1, end);
        fill_expr_piece(stmt.value_expr, value);
        return stmt;
    }

    stmt.kind = StmtKind::Expr;
    fill_expr_piece(stmt.expr, whole);
    return stmt;
}

std::vector<Stmt> Parser::parse_statement_block() {
    std::vector<Stmt> out;
    if (!match(TokenKind::Indent)) {
        return out;
    }
    while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
        if (match(TokenKind::Newline)) {
            continue;
        }
        std::vector<Stmt> children;
        const size_t statement_start = cursor_;
        int bracket_depth = 0;
        int paren_depth = 0;
        int brace_depth = 0;
        while (!at(TokenKind::End)) {
            const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
            if (!inside_group && at(TokenKind::Newline)) {
                break;
            }
            if (current().kind == TokenKind::LBracket) {
                ++bracket_depth;
            } else if (current().kind == TokenKind::RBracket) {
                --bracket_depth;
            } else if (current().kind == TokenKind::LParen) {
                ++paren_depth;
            } else if (current().kind == TokenKind::RParen) {
                --paren_depth;
            } else if (current().kind == TokenKind::LBrace) {
                ++brace_depth;
            } else if (current().kind == TokenKind::RBrace) {
                --brace_depth;
            }
            ++cursor_;
        }
        const size_t statement_end = cursor_;
        consume(TokenKind::Newline, "expected newline after statement");
        if (at(TokenKind::Indent)) {
            children = parse_statement_block();
        }
        const size_t after_statement = cursor_;
        cursor_ = statement_start;
        out.push_back(parse_statement(std::move(children), statement_end));
        cursor_ = after_statement;
    }
    consume(TokenKind::Dedent, "expected dedent after block");
    return out;
}

} // namespace dudu
