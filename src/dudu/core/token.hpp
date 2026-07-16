#pragma once

#include "dudu/core/source.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

enum class TokenKind : uint8_t {
    Identifier,
    Number,
    String,
    Newline,
    Indent,
    Dedent,
    End,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Dot,
    Ellipsis,
    Arrow,
    Operator,
    Assign,
    At,
};

struct Token {
    std::string_view text;
    SourceLocation location;
    TokenKind kind = TokenKind::End;
};

template <size_t N> bool text_is(std::string_view text, const char (&literal)[N]) {
    static_assert(N > 0);
    constexpr size_t literal_size = N - 1;
    return text.size() == literal_size &&
           std::char_traits<char>::compare(text.data(), literal, literal_size) == 0;
}

template <size_t N>
bool token_text_is(const Token& token, TokenKind kind, const char (&literal)[N]) {
    return token.kind == kind && text_is(token.text, literal);
}

SourceLocation token_end_location(const Token& token);
std::string token_kind_name(TokenKind kind);

} // namespace dudu
