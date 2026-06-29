#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/parser/ast_type_token_parser.hpp"
#include "dudu/parser/lexer.hpp"

#include <algorithm>
#include <vector>

namespace dudu {
namespace {

void shift_token_locations(std::vector<Token>& tokens, const SourceLocation& base) {
    for (Token& token : tokens) {
        token.location.file = base.file;
        token.location.line += base.line - 1;
        if (token.location.line == base.line) {
            token.location.column += base.column - 1;
        }
    }
}

std::vector<Token> type_tokens(std::string_view text, const SourceLocation& location) {
    std::vector<Token> tokens = lex_source(text, location.file.str());
    shift_token_locations(tokens, location);
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                [](const Token& token) {
                                    return token.kind == TokenKind::Newline ||
                                           token.kind == TokenKind::Indent ||
                                           token.kind == TokenKind::Dedent;
                                }),
                 tokens.end());
    return tokens;
}

} // namespace

std::vector<TypeRef> parse_type_list(std::string_view text, SourceLocation location) {
    text = trim_view_with_location(text, location);
    if (text.empty()) {
        return {};
    }
    std::vector<Token> tokens = type_tokens(text, location);
    TypeTokenParser parser(tokens);
    return parser.parse_list();
}

TypeRef parse_type_text(std::string_view text, SourceLocation location) {
    text = trim_view_with_location(text, location);
    if (text.empty()) {
        return make_type(TypeKind::Unknown, text, location);
    }
    std::vector<Token> tokens = type_tokens(text, location);
    TypeTokenParser parser(tokens);
    return parser.parse();
}

} // namespace dudu
