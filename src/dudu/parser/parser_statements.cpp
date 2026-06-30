#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/parser/parser_internal.hpp"
#include "dudu/parser/parser_statement_ops.hpp"

#include <optional>
#include <string_view>

namespace dudu {
namespace {

struct UnsupportedStatement {
    std::string_view keyword;
    UnsupportedFeature feature = UnsupportedFeature::None;
};

constexpr UnsupportedStatement unsupported_statements[] = {
    {"finally", UnsupportedFeature::Exceptions},
    {"yield", UnsupportedFeature::Generators},
    {"async", UnsupportedFeature::Async},
    {"await", UnsupportedFeature::Async},
    {"with", UnsupportedFeature::ContextManagers},
    {"global", UnsupportedFeature::GlobalRebinding},
    {"nonlocal", UnsupportedFeature::NonlocalRebinding},
    {"del", UnsupportedFeature::DynamicDeletion},
    {"def", UnsupportedFeature::LocalFunctionDeclarations},
    {"import", UnsupportedFeature::LocalImports},
    {"from", UnsupportedFeature::LocalImports},
};

bool token_is_identifier(const Token& token, std::string_view text) {
    return token.kind == TokenKind::Identifier && token.text == text;
}

template <size_t N> bool token_is_identifier(const Token& token, const char (&text)[N]) {
    return token_text_is(token, TokenKind::Identifier, text);
}

bool token_is_stop(const Token& token, std::initializer_list<TokenKind> stops) {
    for (const TokenKind stop : stops) {
        if (token.kind == stop)
            return true;
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

void reject_tokens_after_block_colon(std::span<const Token> tokens, size_t cursor,
                                     size_t statement_end, std::string_view keyword) {
    if (cursor < statement_end && cursor < tokens.size()) {
        throw CompileError(tokens[cursor].location,
                           "unexpected tokens after " + std::string(keyword) +
                               " header; put the block body on the next indented line",
                           "dudu.parser.syntax");
    }
}

struct StatementOperatorScan {
    std::optional<size_t> colon;
    std::optional<size_t> assignment;
};

StatementOperatorScan scan_top_level_statement_operators(std::span<const Token> tokens,
                                                         size_t begin, size_t end) {
    StatementOperatorScan out;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t index = begin; index < end; ++index) {
        const Token& token = tokens[index];
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group) {
            if (!out.colon.has_value() && token.kind == TokenKind::Colon) {
                out.colon = index;
            }
            if (!out.assignment.has_value() && is_assignment_operator(token)) {
                out.assignment = index;
                break;
            }
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
    return out;
}

UnsupportedFeature unsupported_feature_for_token(const Token& token) {
    if (token.kind != TokenKind::Identifier) {
        return UnsupportedFeature::None;
    }
    for (const UnsupportedStatement& unsupported : unsupported_statements) {
        if (token.text == unsupported.keyword) {
            return unsupported.feature;
        }
    }
    return UnsupportedFeature::None;
}

void attach_statement_range(Stmt& stmt, const Parser::JoinedTokens& joined) {
    if (joined.has_tokens)
        stmt.range = joined.range;
}

std::string declaration_name_from_piece(std::span<const Token> tokens, size_t begin, size_t end) {
    if (begin >= end || begin >= tokens.size() || tokens[begin].kind != TokenKind::Identifier) {
        throw CompileError(begin < tokens.size() ? tokens[begin].location : SourceLocation{},
                           "expected declaration name", "dudu.parser.syntax");
    }
    for (size_t index = begin + 1; index < end && index < tokens.size(); ++index) {
        throw CompileError(tokens[index].location, "expected : after declaration name",
                           "dudu.parser.syntax");
    }
    return std::string(tokens[begin].text);
}

std::optional<Expr> simple_name_expr_from_piece(std::span<const Token> tokens, size_t begin,
                                                size_t end) {
    if (begin + 1 == end && begin < tokens.size() && tokens[begin].kind == TokenKind::Identifier) {
        return make_expr(ExprKind::Name, tokens[begin].text, tokens[begin].location);
    }
    return std::nullopt;
}

} // namespace

std::vector<Parser::JoinedTokens>
Parser::split_top_level_comma_pieces(const JoinedTokens& piece) const {
    std::vector<JoinedTokens> out;
    if (!piece.has_tokens) {
        return out;
    }
    size_t begin = piece.begin;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t index = piece.begin; index < piece.end && index < tokens_.size(); ++index) {
        const Token& token = tokens_[index];
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group && token.kind == TokenKind::Comma) {
            out.push_back(join_tokens(begin, index));
            begin = index + 1;
            continue;
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
    out.push_back(join_tokens(begin, piece.end));
    return out;
}

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

    if (const UnsupportedFeature unsupported = unsupported_feature_for_token(start);
        unsupported != UnsupportedFeature::None) {
        stmt.kind = StmtKind::Unsupported;
        stmt.unsupported_feature = unsupported;
        join_until_with_range({TokenKind::Newline});
        attach_statement_range(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("return")) {
        stmt.kind = StmtKind::Return;
        const size_t value_begin = cursor_;
        cursor_ = statement_end;
        const JoinedTokens value = join_tokens(value_begin, statement_end);
        stmt.value_expr = parse_expr_piece(value);
        attach_statement_range(stmt, join_tokens(begin, statement_end));
        return stmt;
    }

    if (match_identifier("if") || match_identifier("elif") || match_identifier("while") ||
        match_identifier("match")) {
        const std::string keyword{previous().text};
        stmt.kind = keyword == "if"      ? StmtKind::If
                    : keyword == "elif"  ? StmtKind::Elif
                    : keyword == "while" ? StmtKind::While
                                         : StmtKind::Match;
        const JoinedTokens condition =
            join_until_with_range({TokenKind::Colon, TokenKind::Newline});
        set_stmt_condition_expr(stmt, parse_expr_piece(condition));
        consume(TokenKind::Colon, "expected : after " + keyword + " condition");
        reject_tokens_after_block_colon(tokens_, cursor_, statement_end, keyword);
        attach_statement_range(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("else") || match_identifier("try")) {
        const std::string keyword{previous().text};
        stmt.kind = keyword == "else" ? StmtKind::Else : StmtKind::Try;
        consume(TokenKind::Colon, "expected : after " + keyword);
        reject_tokens_after_block_colon(tokens_, cursor_, statement_end, keyword);
        attach_statement_range(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("for")) {
        stmt.kind = StmtKind::For;
        const Token& name = consume_identifier("expected for loop binding");
        stmt.name = name.text;
        stmt.location = name.location;
        if (match(TokenKind::Colon)) {
            const JoinedTokens type = join_until_top_level_identifier("in", {TokenKind::Newline});
            set_stmt_type_ref(stmt, parse_type_piece(type));
        }
        if (!match_identifier("in")) {
            fail_current("expected in after for loop binding");
        }
        const JoinedTokens iterable = join_until_with_range({TokenKind::Colon, TokenKind::Newline});
        set_stmt_iterable_expr(stmt, parse_expr_piece(iterable));
        consume(TokenKind::Colon, "expected : after for loop iterable");
        reject_tokens_after_block_colon(tokens_, cursor_, statement_end, "for");
        attach_statement_range(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("case")) {
        stmt.kind = StmtKind::Case;
        const JoinedTokens pattern = join_until_top_level_identifier("if", {TokenKind::Colon});
        set_stmt_pattern_expr(stmt, parse_expr_piece(pattern));
        if (match_identifier("if")) {
            const JoinedTokens guard =
                join_until_with_range({TokenKind::Colon, TokenKind::Newline});
            set_stmt_guard_expr(stmt, parse_expr_piece(guard));
        }
        consume(TokenKind::Colon, "expected : after case pattern");
        reject_tokens_after_block_colon(tokens_, cursor_, statement_end, "case");
        attach_statement_range(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("except")) {
        stmt.kind = StmtKind::Except;
        if (at(TokenKind::Colon)) {
            consume(TokenKind::Colon, "expected : after except");
            reject_tokens_after_block_colon(tokens_, cursor_, statement_end, "except");
            attach_statement_range(stmt, join_tokens(begin, cursor_));
            return stmt;
        }
        const JoinedTokens header = join_until_with_range({TokenKind::Colon, TokenKind::Newline});
        const bool binding_header =
            at(TokenKind::Colon) && has_statement_tokens_after(tokens_, cursor_ + 1);
        if (binding_header) {
            stmt.name = declaration_name_from_piece(tokens_, header.begin, header.end);
            consume(TokenKind::Colon, "expected : after except binding");
            const JoinedTokens type = join_until_with_range({TokenKind::Colon, TokenKind::Newline});
            set_stmt_type_ref(stmt, parse_type_piece(type));
        } else {
            set_stmt_condition_expr(stmt, parse_expr_piece(header));
        }
        consume(TokenKind::Colon, "expected : after except");
        reject_tokens_after_block_colon(tokens_, cursor_, statement_end, "except");
        attach_statement_range(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("break") || match_identifier("continue") || match_identifier("pass")) {
        const std::string keyword{previous().text};
        stmt.kind = keyword == "break"      ? StmtKind::Break
                    : keyword == "continue" ? StmtKind::Continue
                                            : StmtKind::Pass;
        join_until_with_range({TokenKind::Newline});
        attach_statement_range(stmt, join_tokens(begin, cursor_));
        return stmt;
    }

    if (match_identifier("raise") || match_identifier("delete")) {
        const std::string keyword{previous().text};
        stmt.kind = keyword == "raise" ? StmtKind::Raise : StmtKind::Delete;
        const size_t value_begin = cursor_;
        cursor_ = statement_end;
        const JoinedTokens value = join_tokens(value_begin, statement_end);
        stmt.value_expr = parse_expr_piece(value);
        attach_statement_range(stmt, join_tokens(begin, statement_end));
        return stmt;
    }

    if (match_identifier("assert") || match_identifier("debug_assert")) {
        const std::string keyword{previous().text};
        stmt.kind = keyword == "assert" ? StmtKind::Assert : StmtKind::DebugAssert;
        const size_t parts_begin = cursor_;
        cursor_ = statement_end;
        const std::vector<JoinedTokens> parts =
            split_top_level_comma_pieces(join_tokens(parts_begin, statement_end));
        if (!parts.empty()) {
            set_stmt_condition_expr(stmt, parse_expr_piece(parts.front()));
        }
        if (parts.size() >= 2) {
            set_stmt_message_expr(stmt, parse_expr_piece(parts[1]));
        }
        attach_statement_range(stmt, join_tokens(begin, statement_end));
        return stmt;
    }

    if (check_text("cpp")) {
        stmt.kind = StmtKind::CppEscape;
        join_until_with_range({TokenKind::Newline});
        const JoinedTokens source = join_tokens(begin, cursor_);
        stmt.cpp_lines =
            cpp_escape_lines(cpp_escape_body(token_source_spelling(source.begin, source.end)));
        attach_statement_range(stmt, source);
        return stmt;
    }

    const size_t line_begin = cursor_;
    cursor_ = statement_end;
    const size_t end = cursor_;
    const JoinedTokens whole = join_tokens(line_begin, end);
    attach_statement_range(stmt, whole);

    const StatementOperatorScan operators =
        scan_top_level_statement_operators(tokens_, line_begin, end);
    const std::optional<size_t> colon = operators.colon;
    const std::optional<size_t> assignment = operators.assignment;
    if (colon.has_value() && (!assignment.has_value() || *colon < *assignment)) {
        stmt.kind = StmtKind::VarDecl;
        stmt.name = declaration_name_from_piece(tokens_, line_begin, *colon);
        const size_t type_end = assignment.value_or(end);
        const JoinedTokens type = join_tokens(*colon + 1, type_end);
        set_stmt_type_ref(stmt, parse_type_piece(type));
        if (assignment.has_value()) {
            const JoinedTokens value = join_tokens(*assignment + 1, end);
            stmt.value_expr = parse_expr_piece(value);
        }
        return stmt;
    }

    if (assignment.has_value()) {
        const Token& assign = tokens_[*assignment];
        stmt.kind =
            is_compound_assignment_operator(assign) ? StmtKind::CompoundAssign : StmtKind::Assign;
        if (stmt.kind == StmtKind::CompoundAssign) {
            const std::optional<CompoundAssignOp> op = compound_assignment_op(assign.text);
            if (!op) {
                throw CompileError(assign.location, "unsupported compound assignment operator: " +
                                                        std::string(assign.text));
            }
            stmt.compound_op = *op;
        }
        const JoinedTokens target = join_tokens(line_begin, *assignment);
        if (std::optional<Expr> simple_target =
                simple_name_expr_from_piece(tokens_, line_begin, *assignment)) {
            set_stmt_target_expr(stmt, std::move(*simple_target));
        } else {
            set_stmt_target_expr(stmt, parse_expr_piece(target));
        }
        const JoinedTokens value = join_tokens(*assignment + 1, end);
        stmt.value_expr = parse_expr_piece(value);
        return stmt;
    }

    stmt.kind = StmtKind::Expr;
    stmt.expr = parse_expr_piece(whole);
    return stmt;
}

std::vector<Stmt> Parser::parse_statement_block() {
    std::vector<Stmt> out;
    if (!match(TokenKind::Indent)) {
        return out;
    }
    out.reserve(estimate_statement_block_capacity());
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
        size_t statement_end = cursor_;
        consume(TokenKind::Newline, "expected newline after statement");
        if (starts_statement_continuation(cursor_)) {
            statement_end = consume_statement_continuation_block();
        }
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

size_t Parser::estimate_statement_block_capacity() const {
    size_t count = 0;
    int nested_indent = 0;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    bool saw_top_level_token = false;

    for (size_t index = cursor_; index < tokens_.size(); ++index) {
        const Token& token = tokens_[index];
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group) {
            if (token.kind == TokenKind::Indent) {
                ++nested_indent;
                continue;
            }
            if (token.kind == TokenKind::Dedent) {
                if (nested_indent == 0) {
                    break;
                }
                --nested_indent;
                continue;
            }
        }

        if (nested_indent == 0) {
            if (!inside_group && token.kind == TokenKind::Newline) {
                if (saw_top_level_token) {
                    ++count;
                    saw_top_level_token = false;
                }
                continue;
            }
            if (token.kind != TokenKind::Newline && token.kind != TokenKind::Indent &&
                token.kind != TokenKind::Dedent) {
                saw_top_level_token = true;
            }
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

    if (saw_top_level_token) {
        ++count;
    }
    return count;
}

} // namespace dudu
