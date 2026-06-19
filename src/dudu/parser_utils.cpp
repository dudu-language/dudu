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
