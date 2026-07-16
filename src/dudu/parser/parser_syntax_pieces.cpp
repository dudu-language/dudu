#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_expr_token_parser.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/parser/ast_type_token_parser.hpp"
#include "dudu/parser/parser_internal.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

void append_source_token(std::ostringstream& out, SourceLocation& cursor, const Token& token) {
    while (cursor.line < token.location.line) {
        out << '\n';
        ++cursor.line;
        cursor.column = 1;
    }
    while (cursor.column < token.location.column) {
        out << ' ';
        ++cursor.column;
    }
    out << token.text;
    cursor = token_end_location(token);
}

const TypeRef* malformed_type_node(const TypeRef& type) {
    if (type.kind == TypeKind::Unknown && type.malformed) {
        return &type;
    }
    for (const TypeRef& child : type.children) {
        if (const TypeRef* malformed = malformed_type_node(child)) {
            return malformed;
        }
    }
    return nullptr;
}

std::vector<Token> syntax_piece_tokens(std::span<const Token> tokens) {
    std::vector<Token> out;
    out.reserve(tokens.size());
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
            token.kind == TokenKind::Dedent) {
            continue;
        }
        out.push_back(token);
    }
    SourceLocation end_location;
    if (!out.empty()) {
        end_location = token_end_location(out.back());
    }
    out.push_back({.text = "", .location = end_location, .kind = TokenKind::End});
    return out;
}

} // namespace

Parser::JoinedTokens Parser::join_until_with_range(std::initializer_list<TokenKind> stops) {
    JoinedTokens joined;
    joined.begin = cursor_;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    int layout_depth = 0;
    while (!at(TokenKind::End)) {
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (recovering() && inside_group && at(TokenKind::Newline) && layout_depth == 0 &&
            !at_next(TokenKind::Indent)) {
            throw CompileError(current().location, "unfinished expression group before end of line",
                               "dudu.parser.unfinished_group");
        }
        if (!inside_group) {
            bool stop = false;
            for (TokenKind kind : stops) {
                stop = stop || at(kind);
            }
            if (stop) {
                break;
            }
        }
        if (inside_group &&
            (at(TokenKind::Newline) || at(TokenKind::Indent) || at(TokenKind::Dedent))) {
            joined.has_layout_tokens = true;
            if (at(TokenKind::Indent)) {
                ++layout_depth;
            } else if (at(TokenKind::Dedent)) {
                layout_depth = std::max(0, layout_depth - 1);
            }
            ++cursor_;
            continue;
        }
        const Token& token = current();
        if (!joined.has_tokens) {
            joined.range.start = token.location;
            joined.has_tokens = true;
        }
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
            token.kind == TokenKind::Dedent) {
            joined.has_layout_tokens = true;
        }
        joined.range.end = token_end_location(token);
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
    joined.end = cursor_;
    return joined;
}

Parser::JoinedTokens Parser::join_tokens(size_t begin, size_t end) const {
    JoinedTokens joined;
    joined.begin = begin;
    joined.end = std::min(end, tokens_.size());
    if (begin < joined.end && begin < tokens_.size()) {
        const Token& first = tokens_[begin];
        const Token& last = tokens_[joined.end - 1];
        if (first.kind != TokenKind::Newline && first.kind != TokenKind::Indent &&
            first.kind != TokenKind::Dedent && last.kind != TokenKind::Newline &&
            last.kind != TokenKind::Indent && last.kind != TokenKind::Dedent &&
            first.location.line == last.location.line) {
            joined.has_tokens = true;
            joined.range.start = first.location;
            joined.range.end = token_end_location(last);
            return joined;
        }
    }
    for (size_t index = begin; index < end && index < tokens_.size(); ++index) {
        const Token& token = tokens_[index];
        if (!joined.has_tokens) {
            joined.range.start = token.location;
            joined.has_tokens = true;
        }
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
            token.kind == TokenKind::Dedent) {
            joined.has_layout_tokens = true;
        }
        joined.range.end = token_end_location(token);
    }
    return joined;
}

std::string Parser::token_source_spelling(size_t begin, size_t end) const {
    std::ostringstream out;
    SourceLocation source_cursor;
    bool has_tokens = false;
    for (size_t index = begin; index < end && index < tokens_.size(); ++index) {
        const Token& token = tokens_[index];
        if (!has_tokens) {
            source_cursor = token.location;
            has_tokens = true;
        }
        append_source_token(out, source_cursor, token);
    }
    return out.str();
}

Expr Parser::parse_expr_piece(const JoinedTokens& piece) const {
    if (!piece.has_tokens) {
        return make_expr(ExprKind::Missing, "", piece.range.start);
    }
    const std::span<const Token> source_tokens =
        tokens_.subspan(piece.begin, piece.end - piece.begin);
    std::vector<Token> filtered_tokens;
    if (piece.has_layout_tokens) {
        filtered_tokens = syntax_piece_tokens(source_tokens);
    }
    ExprTokenParser parser(filtered_tokens.empty() ? source_tokens
                                                   : std::span<const Token>{filtered_tokens});
    Expr expr = parser.parse();
    if (expr.kind == ExprKind::Unknown) {
        const std::string spelling = trim_string(token_source_spelling(piece.begin, piece.end));
        throw CompileError(expr.location,
                           "unsupported expression: " +
                               (spelling.empty() ? display_expr(expr) : spelling),
                           "dudu.parser.unsupported_expression");
    }
    return expr;
}

TypeRef Parser::parse_type_piece(const JoinedTokens& piece) const {
    if (!piece.has_tokens) {
        return make_type(TypeKind::Unknown, "", piece.range.start);
    }
    const std::span<const Token> source_tokens =
        tokens_.subspan(piece.begin, piece.end - piece.begin);
    std::vector<Token> filtered_tokens;
    if (piece.has_layout_tokens) {
        filtered_tokens = syntax_piece_tokens(source_tokens);
    }
    TypeTokenParser parser(filtered_tokens.empty() ? source_tokens
                                                   : std::span<const Token>{filtered_tokens});
    TypeRef type = parser.parse();
    if (const TypeRef* malformed = malformed_type_node(type)) {
        std::string spelling = trim_string(token_source_spelling(piece.begin, piece.end));
        const std::string message =
            spelling.empty() ? "malformed type syntax" : "malformed type syntax: " + spelling;
        throw CompileError(malformed->location.line > 0 ? malformed->location : type.location,
                           message, "dudu.parser.malformed_type");
    }
    return type;
}

} // namespace dudu
