#include "dudu/ast_expr_token_parser.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/lexer.hpp"

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

} // namespace

SourceLocation expr_token_end_location(const Token& token) {
    SourceLocation end = token.location;
    end.column += static_cast<int>(token.text.size());
    return end;
}

Expr parse_expr_text(std::string_view text, SourceLocation location) {
    text = trim_view_with_location(text, location);
    if (text.empty()) {
        return make_expr(ExprKind::Missing, "", location);
    }
    std::vector<Token> tokens = lex_source(text, location.file.str());
    shift_token_locations(tokens, location);
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                [](const Token& token) {
                                    return token.kind == TokenKind::Newline ||
                                           token.kind == TokenKind::Indent ||
                                           token.kind == TokenKind::Dedent;
                                }),
                 tokens.end());
    ExprTokenParser parser(tokens);
    return parser.parse();
}

} // namespace dudu
