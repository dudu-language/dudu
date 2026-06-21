#include "dudu/parser_internal.hpp"

namespace dudu {
namespace {

bool is_statement_continuation_operator(const Token& token) {
    if (token.kind == TokenKind::Identifier) {
        return token.text == "and" || token.text == "or";
    }
    if (token.kind != TokenKind::Operator) {
        return false;
    }
    return token.text == "+" || token.text == "-" || token.text == "*" || token.text == "/" ||
           token.text == "%" || token.text == "&" || token.text == "|" || token.text == "^" ||
           token.text == "<<" || token.text == ">>" || token.text == "==" ||
           token.text == "!=" || token.text == "<" || token.text == "<=" || token.text == ">" ||
           token.text == ">=";
}

} // namespace

bool Parser::starts_statement_continuation(size_t cursor) const {
    return cursor + 1 < tokens_.size() && tokens_[cursor].kind == TokenKind::Indent &&
           is_statement_continuation_operator(tokens_[cursor + 1]);
}

size_t Parser::consume_statement_continuation_block() {
    consume(TokenKind::Indent, "expected indent before expression continuation");
    size_t continuation_end = cursor_;
    while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
        if (at(TokenKind::Newline)) {
            ++cursor_;
            continue;
        }
        if (!is_statement_continuation_operator(current())) {
            fail_current("expected expression continuation operator");
        }
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
        continuation_end = cursor_;
        consume(TokenKind::Newline, "expected newline after expression continuation");
    }
    consume(TokenKind::Dedent, "expected dedent after expression continuation");
    return continuation_end;
}

} // namespace dudu
