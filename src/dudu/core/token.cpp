#include "dudu/core/token.hpp"

namespace dudu {

SourceLocation token_end_location(const Token& token) {
    SourceLocation end = token.location;
    for (const char c : token.text) {
        if (c == '\n') {
            ++end.line;
            end.column = 1;
        } else {
            ++end.column;
        }
    }
    return end;
}

std::string token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::Number:
        return "number";
    case TokenKind::String:
        return "string";
    case TokenKind::Newline:
        return "newline";
    case TokenKind::Indent:
        return "indent";
    case TokenKind::Dedent:
        return "dedent";
    case TokenKind::End:
        return "end";
    case TokenKind::LParen:
        return "(";
    case TokenKind::RParen:
        return ")";
    case TokenKind::LBracket:
        return "[";
    case TokenKind::RBracket:
        return "]";
    case TokenKind::LBrace:
        return "{";
    case TokenKind::RBrace:
        return "}";
    case TokenKind::Comma:
        return ",";
    case TokenKind::Colon:
        return ":";
    case TokenKind::Dot:
        return ".";
    case TokenKind::Ellipsis:
        return "...";
    case TokenKind::Arrow:
        return "->";
    case TokenKind::Operator:
        return "operator";
    case TokenKind::Assign:
        return "=";
    case TokenKind::At:
        return "@";
    }
    return "unknown";
}

} // namespace dudu
