#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/token.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace dudu {

class TypeTokenParser {
  public:
    explicit TypeTokenParser(std::span<const Token> tokens);

    TypeRef parse();
    std::vector<TypeRef> parse_list();

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
    bool at_ellipsis() const;
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
    bool match_scope_separator();
    bool match_ellipsis();

    SourceRange range_between(size_t begin, size_t end) const;
    std::string text_between(size_t begin, size_t end) const;
    TypeRef make_node(TypeKind kind, size_t begin, size_t end) const;

    TypeRef parse_type(std::initializer_list<TokenKind> stops);
    TypeRef parse_prefix(std::initializer_list<TokenKind> stops);
    TypeRef parse_primary(std::initializer_list<TokenKind> stops);
    TypeRef parse_function_type(size_t begin, std::vector<TypeRef> params);
    TypeRef parse_fn_type(size_t begin);
    TypeRef parse_paren_or_function(size_t begin, std::initializer_list<TokenKind> stops);
    TypeRef parse_c_tag_name(size_t begin);
    TypeRef parse_name_or_template(size_t begin);
    std::vector<TypeRef> parse_angle_template_args();
    std::vector<TypeRef> parse_list_until(TokenKind close);
    TypeRef parse_shape_dim(size_t begin, size_t end) const;
    std::vector<TypeRef> parse_shape_list_until(TokenKind close);
};

} // namespace dudu
