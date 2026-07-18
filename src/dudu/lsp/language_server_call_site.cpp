#include "dudu/lsp/language_server_call_site.hpp"

#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/parser/lexer.hpp"

#include <optional>
#include <vector>

namespace dudu {
namespace {

bool token_before_cursor(const Token& token, int line, int character) {
    if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
        token.kind == TokenKind::Dedent || token.kind == TokenKind::End) {
        return false;
    }
    return token.location.line - 1 == line && token.location.column - 1 < character;
}

std::vector<Token> tokens_before_cursor(const Document& doc, int line, int character) {
    std::vector<Token> out;
    const LexResult lexed = lex_source_recovering(doc.text, doc.path);
    for (const Token& token : lexed.tokens) {
        if (token_before_cursor(token, line, character)) {
            out.push_back(token);
        }
    }
    return out;
}

std::string call_name_before_open_paren(const std::vector<Token>& tokens, size_t open_index) {
    if (open_index == 0) {
        return {};
    }
    size_t name_end = open_index - 1;
    if (tokens[name_end].kind == TokenKind::RBracket) {
        int bracket_depth = 0;
        std::optional<size_t> template_open;
        for (size_t index = name_end + 1; index-- > 0;) {
            if (tokens[index].kind == TokenKind::RBracket) {
                ++bracket_depth;
            } else if (tokens[index].kind == TokenKind::LBracket) {
                --bracket_depth;
                if (bracket_depth == 0) {
                    template_open = index;
                    break;
                }
            }
        }
        if (!template_open || *template_open == 0) {
            return {};
        }
        name_end = *template_open - 1;
    }
    if (tokens[name_end].kind != TokenKind::Identifier) {
        return {};
    }
    size_t start = name_end;
    while (start >= 2 && tokens[start - 1].kind == TokenKind::Dot &&
           tokens[start - 2].kind == TokenKind::Identifier) {
        start -= 2;
    }
    std::string out;
    for (size_t index = start; index <= name_end; ++index) {
        if (tokens[index].kind != TokenKind::Identifier && tokens[index].kind != TokenKind::Dot) {
            return {};
        }
        out += tokens[index].text;
    }
    return out;
}

} // namespace

LspCallSite lsp_call_site_at(const Document& doc, const Json* params) {
    const LspPosition position = lsp_position(params);
    const std::vector<Token> tokens = tokens_before_cursor(doc, position.line, position.character);
    int depth = 0;
    int parameter = 0;
    for (size_t index = tokens.size(); index-- > 0;) {
        const Token& token = tokens[index];
        if (token.kind == TokenKind::RParen) {
            ++depth;
            continue;
        }
        if (token.kind == TokenKind::LParen) {
            if (depth > 0) {
                --depth;
                continue;
            }
            return {.name = call_name_before_open_paren(tokens, index), .parameter = parameter};
        }
        if (token.kind == TokenKind::Comma && depth == 0) {
            ++parameter;
        }
    }
    return {};
}

} // namespace dudu
