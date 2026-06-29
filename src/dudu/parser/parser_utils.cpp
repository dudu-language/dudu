#include "dudu/parser/parser_utils.hpp"

#include "dudu/core/source.hpp"

#include <map>

namespace dudu {
namespace {

bool is_foreign_import(const ImportDecl& import) {
    return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCxx ||
           import.kind == ImportKind::ForeignCpp;
}

bool is_ascii_upper(char c) {
    return c >= 'A' && c <= 'Z';
}

bool is_ascii_lower(char c) {
    return c >= 'a' && c <= 'z';
}

bool is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

} // namespace

bool is_all_caps_identifier(const Token& token) {
    if (token.kind != TokenKind::Identifier || token.text.empty()) {
        return false;
    }
    bool saw_letter = false;
    for (char c : token.text) {
        if (is_ascii_upper(c) || is_ascii_lower(c)) {
            saw_letter = true;
            if (is_ascii_lower(c)) {
                return false;
            }
        } else if (!is_ascii_digit(c) && c != '_') {
            return false;
        }
    }
    return saw_letter;
}

void validate_import_bindings(const std::vector<ImportDecl>& imports) {
    std::map<std::string, ImportDecl> direct;
    for (const ImportDecl& import : imports) {
        if ((import.kind == ImportKind::Module || is_foreign_import(import)) &&
            import.alias.empty()) {
            continue;
        }
        const std::string name = bound_import_name(import);
        const auto [it, inserted] = direct.emplace(name, import);
        if (!inserted) {
            if (is_foreign_import(import) && is_foreign_import(it->second)) {
                continue;
            }
            throw CompileError(import.location,
                               "import name '" + name +
                                   "' collides with an earlier direct import; use 'as' to choose "
                                   "a unique local name");
        }
    }
}

} // namespace dudu
