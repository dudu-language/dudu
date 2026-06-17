#include "dudu/ast_parse_utils.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/parser_internal.hpp"

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
    stmt.text = joined.text;
    stmt.source_text = joined.source_text.empty() ? joined.text : joined.source_text;
    if (joined.has_tokens) {
        stmt.range = joined.range;
    } else {
        stmt.range = range_for_text(stmt.location, stmt.text);
    }
}

void parse_assert_parts(Stmt& stmt, const Parser::JoinedTokens& body) {
    const std::vector<CommaPart> parts = split_top_level_comma_parts(body.text);
    if (parts.empty()) {
        return;
    }
    stmt.condition = trim_string(parts.front().text);
    stmt.condition_expr =
        parse_expr_text(stmt.condition, advance_columns(body.range.start, parts.front().offset));
    if (parts.size() >= 2) {
        stmt.message = trim_string(parts[1].text);
        stmt.message_expr =
            parse_expr_text(stmt.message, advance_columns(body.range.start, parts[1].offset));
    }
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

Stmt Parser::parse_statement(std::vector<Stmt> children) {
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
        stmt.value = value.text;
        stmt.value_expr = parse_expr_text(stmt.value, value.range.start);
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
        stmt.condition = condition.text;
        stmt.condition_expr = parse_expr_text(stmt.condition, condition.range.start);
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
        stmt.iterable = iterable.text;
        stmt.iterable_expr = parse_expr_text(stmt.iterable, iterable.range.start);
        consume(TokenKind::Colon, "expected : after for loop iterable");
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("case")) {
        stmt.kind = StmtKind::Case;
        const JoinedTokens pattern = join_until_top_level_identifier("if", {TokenKind::Colon});
        stmt.pattern = pattern.text;
        stmt.pattern_expr = parse_expr_text(stmt.pattern, pattern.range.start);
        if (match_identifier("if")) {
            const JoinedTokens guard =
                join_until_with_range({TokenKind::Colon, TokenKind::Newline});
            stmt.guard = guard.text;
            stmt.guard_expr = parse_expr_text(stmt.guard, guard.range.start);
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
            stmt.condition = header.text;
            stmt.condition_expr = parse_expr_text(stmt.condition, header.range.start);
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
        stmt.value = value.text;
        stmt.value_expr = parse_expr_text(stmt.value, value.range.start);
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
        attach_statement_source(stmt, join_tokens(begin, cursor_));
        stmt.value = cpp_escape_body(stmt.text);
        return stmt;
    }

    JoinedTokens joined = join_until_with_range({TokenKind::Newline});
    stmt = statement_from_text(std::move(joined.text), std::move(joined.source_text),
                               start.location, joined.range, std::move(stmt.children));
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
        consume(TokenKind::Newline, "expected newline after statement");
        if (at(TokenKind::Indent)) {
            children = parse_statement_block();
        }
        const size_t after_statement = cursor_;
        cursor_ = statement_start;
        out.push_back(parse_statement(std::move(children)));
        cursor_ = after_statement;
    }
    consume(TokenKind::Dedent, "expected dedent after block");
    return out;
}

} // namespace dudu
