#pragma once

#include "dudu/source.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dudu {

enum class TokenKind {
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
    Arrow,
    Operator,
    Assign,
    At,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string_view text;
    SourceLocation location;
};

std::string token_kind_name(TokenKind kind);

} // namespace dudu
