#include "dudu/parser_utils.hpp"

#include "dudu/source.hpp"

#include <cctype>
#include <map>

namespace dudu {
namespace {

bool is_foreign_import(const ImportDecl& import) {
    return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp;
}

} // namespace

bool is_all_caps_identifier(const Token& token) {
    if (token.kind != TokenKind::Identifier || token.text.empty()) {
        return false;
    }
    bool saw_letter = false;
    for (char c : token.text) {
        if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
            saw_letter = true;
            if (std::islower(static_cast<unsigned char>(c)) != 0) {
                return false;
            }
        } else if (std::isdigit(static_cast<unsigned char>(c)) == 0 && c != '_') {
            return false;
        }
    }
    return saw_letter;
}

bool parser_needs_space_between(TokenKind previous, TokenKind current) {
    if (current == TokenKind::Comma || current == TokenKind::Colon ||
        current == TokenKind::RParen || current == TokenKind::RBracket ||
        current == TokenKind::RBrace || current == TokenKind::Dot || current == TokenKind::LParen ||
        current == TokenKind::LBracket || current == TokenKind::LBrace) {
        return false;
    }
    if (previous == TokenKind::Dot || previous == TokenKind::LParen ||
        previous == TokenKind::LBracket || previous == TokenKind::LBrace) {
        return false;
    }
    return true;
}

void validate_import_bindings(const std::vector<ImportDecl>& imports) {
    std::map<std::string, ImportDecl> direct;
    for (const ImportDecl& import : imports) {
        if (import.kind == ImportKind::Module && import.alias.empty()) {
            continue;
        }
        const std::string name = bound_import_name(import);
        const auto [it, inserted] = direct.emplace(name, import);
        if (!inserted) {
            if (is_foreign_import(import) && is_foreign_import(it->second)) {
                continue;
            }
            throw CompileError(import.location,
                               "import name '" + name + "' collides with an earlier direct import");
        }
    }
}

} // namespace dudu
