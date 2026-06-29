#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/token.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

SourceLocation expr_token_end_location(const Token& token);
inline bool expression_token(const Token& token) {
    return token.kind != TokenKind::Newline && token.kind != TokenKind::Indent &&
           token.kind != TokenKind::Dedent && token.kind != TokenKind::End;
}

class ExprTokenParser {
  public:
    explicit ExprTokenParser(std::span<const Token> tokens);

    Expr parse();

  private:
    std::span<const Token> tokens_;
    size_t cursor_ = 0;

    bool at_end() const;
    const Token& current() const;
    bool at(TokenKind kind) const;
    bool at_operator(std::string_view op) const;
    template <size_t N> bool at_operator(const char (&op)[N]) const {
        return !at_end() && token_text_is(current(), TokenKind::Operator, op);
    }
    bool at_identifier(std::string_view text) const;
    template <size_t N> bool at_identifier(const char (&text)[N]) const {
        return !at_end() && token_text_is(current(), TokenKind::Identifier, text);
    }
    bool stop_at(std::initializer_list<TokenKind> stops) const;
    bool match(TokenKind kind);
    bool match_operator(std::string_view op);
    template <size_t N> bool match_operator(const char (&op)[N]) {
        if (!at_operator(op)) {
            return false;
        }
        ++cursor_;
        return true;
    }
    bool match_identifier(std::string_view text);
    template <size_t N> bool match_identifier(const char (&text)[N]) {
        if (!at_identifier(text)) {
            return false;
        }
        ++cursor_;
        return true;
    }

    SourceRange range_between(size_t begin, size_t end) const;
    std::string text_between(size_t begin, size_t end) const;
    Expr make_node(ExprKind kind, size_t begin, size_t end) const;
    Expr make_node_from_start(ExprKind kind, SourceLocation start, size_t end) const;
    Expr parse_expr_span(size_t begin, size_t end) const;
    TypeRef parse_type_span(size_t begin, size_t end) const;
    std::vector<TypeRef> parse_type_list_span(size_t begin, size_t end) const;

    Expr parse_comma_expr(std::initializer_list<TokenKind> stops);
    Expr parse_named_or_binary(std::initializer_list<TokenKind> stops);
    static int binary_precedence(const Token& token);
    Expr parse_binary(int min_precedence, std::initializer_list<TokenKind> stops);
    Expr parse_prefix(std::initializer_list<TokenKind> stops);
    Expr parse_unsupported_expr(ExprKind kind, size_t begin,
                                std::initializer_list<TokenKind> stops);
    Expr parse_unary(std::string_view op, size_t begin, std::initializer_list<TokenKind> stops);

    Expr parse_postfix(std::initializer_list<TokenKind> stops);
    size_t matching_close(size_t open, TokenKind open_kind, TokenKind close_kind) const;
    bool span_has_top_level_identifier(size_t begin, size_t end, std::string_view text) const;
    size_t expr_token_begin(const Expr& expr) const;
    size_t expr_token_end(const Expr& expr) const;
    Expr parse_call(Expr callee, std::initializer_list<TokenKind> stops);
    Expr parse_template_call(Expr indexed_callee, std::initializer_list<TokenKind> stops);
    Expr parse_template_call_from_brackets(Expr callee, SourceLocation start, size_t template_begin,
                                           size_t template_end,
                                           std::initializer_list<TokenKind> stops);
    std::vector<Expr> parse_arg_list(TokenKind close);
    Expr parse_index_argument();
    Expr parse_primary(std::initializer_list<TokenKind> stops);
    Expr parse_brace_literal(size_t begin);
    bool pointer_cast_call_ahead() const;
    Expr parse_pointer_cast_call();
};

} // namespace dudu
