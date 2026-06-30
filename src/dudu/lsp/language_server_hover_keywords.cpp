#include "dudu/lsp/language_server_hover_keywords.hpp"

#include "dudu/core/token.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/parser/lexer.hpp"

#include <map>
#include <optional>
#include <string>

namespace dudu {
namespace {

std::string fenced_code(std::string_view language, const std::string& code) {
    return "```" + std::string(language) + "\n" + code + "\n```";
}

std::optional<Token> token_at_cursor(const Document& doc, const Json* params) {
    if (params == nullptr) {
        return std::nullopt;
    }
    const LspPosition position = lsp_position(params);
    try {
        for (const Token& token : lex_source(doc.text, doc.path)) {
            if (token.kind != TokenKind::Identifier || token.location.line != position.line + 1) {
                continue;
            }
            const int start = token.location.column - 1;
            const int end = start + static_cast<int>(token.text.size());
            if (position.character >= start && position.character <= end) {
                return token;
            }
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<std::string> keyword_description(std::string_view keyword) {
    static const std::map<std::string, std::string> keywords = {
        {"as", "Binds an imported module or native header to a local alias."},
        {"assert", "Checks a condition and fails the current test or program when it is false."},
        {"break", "Leaves the nearest enclosing loop."},
        {"case", "Declares one branch inside a `match` statement."},
        {"class", "Declares a Dudu class, lowered to a C++ struct/class-shaped type."},
        {"const", "Declares a compile-time constant binding."},
        {"continue", "Skips to the next iteration of the nearest enclosing loop."},
        {"def", "Declares a function or method."},
        {"debug_assert", "Checks a condition in debug builds only."},
        {"delete", "Destroys and frees a heap object created with `new[T](...)`."},
        {"elif", "Adds another branch to an `if` statement."},
        {"else", "Fallback branch for `if`, `match`, or exception handling."},
        {"enum", "Declares an enum or sum type."},
        {"except", "Handles a thrown C++ exception in a `try` statement."},
        {"for", "Iterates over a range or iterable value."},
        {"from", "Imports selected symbols from another Dudu module."},
        {"if", "Runs a block only when a condition is true."},
        {"import", "Imports a Dudu module or a native C/C++ header."},
        {"in", "Separates the loop binding from the iterable in a `for` statement."},
        {"match", "Selects a branch by value or pattern."},
        {"pass", "Explicit empty statement."},
        {"private", "Restricts a declaration to its module or owning type."},
        {"pub", "Exports a declaration from its module."},
        {"public", "Exports a declaration from its module."},
        {"raise", "Throws a C++ exception value."},
        {"return", "Returns from the current function."},
        {"static", "Declares class-owned data or behavior."},
        {"static_assert", "Checks a compile-time condition."},
        {"try", "Starts a C++ exception handling block."},
        {"while", "Repeats a block while a condition stays true."},
    };
    const auto found = keywords.find(std::string(keyword));
    if (found == keywords.end()) {
        return std::nullopt;
    }
    return found->second;
}

} // namespace

std::optional<std::string> keyword_hover_json(const Document& doc, const Json* params) {
    const std::optional<Token> token = token_at_cursor(doc, params);
    if (!token) {
        return std::nullopt;
    }
    const std::optional<std::string> description = keyword_description(token->text);
    if (!description) {
        return std::nullopt;
    }
    const std::string keyword(token->text);
    const std::string markdown = fenced_code("dudu", "keyword " + keyword) + "\n\n" + *description;
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" +
           range_json(token->location.line - 1, token->location.column - 1,
                      token->location.column - 1 + static_cast<int>(token->text.size())) +
           "}";
}

} // namespace dudu
