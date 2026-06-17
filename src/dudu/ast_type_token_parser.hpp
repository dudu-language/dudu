#pragma once

#include "dudu/ast.hpp"
#include "dudu/token.hpp"

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
    bool at_identifier(std::string_view text) const;
    bool stop_at(std::initializer_list<TokenKind> stops) const;
    bool match(TokenKind kind);
    bool match_operator(std::string_view op);
    bool match_identifier(std::string_view text);

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
    std::vector<TypeRef> parse_list_until(TokenKind close);
};

} // namespace dudu
