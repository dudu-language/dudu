#include "dudu/language_server_hover.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string doc_comment_before(const Document& doc, int one_based_line) {
    const std::vector<std::string> lines = document_lines(doc.text);
    int row = one_based_line - 2;
    std::vector<std::string> comments;
    while (row >= 0 && row < static_cast<int>(lines.size())) {
        const std::string trimmed = trim_copy(lines[static_cast<size_t>(row)]);
        if (!starts_with(trimmed, "#")) {
            break;
        }
        comments.push_back(trim_copy(trimmed.substr(1)));
        --row;
    }
    std::reverse(comments.begin(), comments.end());
    std::ostringstream out;
    for (size_t i = 0; i < comments.size(); ++i) {
        if (i > 0) {
            out << "\n";
        }
        out << comments[i];
    }
    return out.str();
}

std::optional<Document> imported_document(const Document& doc, const ImportDecl& import) {
    const std::filesystem::path file =
        module_path_to_file(doc.path.parent_path(), import.module_path);
    std::error_code error;
    if (!std::filesystem::exists(file, error) || error) {
        return std::nullopt;
    }
    std::ifstream input(file);
    if (!input) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return Document{.uri = file_uri(file), .path = file, .text = text};
}

std::optional<std::string> imported_symbol_hover_json(const Document& doc,
                                                      const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ImportDecl& import : module.imports) {
            if (import.kind == ImportKind::Module) {
                const std::string bound = bound_import_name(import);
                const std::vector<std::string> prefixes =
                    import.alias.empty() ? std::vector<std::string>{import.module_path, bound}
                                         : std::vector<std::string>{bound};
                std::string imported_name;
                for (const std::string& prefix : prefixes) {
                    if (word.rfind(prefix + ".", 0) == 0) {
                        imported_name = word.substr(prefix.size() + 1);
                        break;
                    }
                }
                if (imported_name.empty()) {
                    continue;
                }
                const std::optional<Document> imported = imported_document(doc, import);
                if (!imported) {
                    continue;
                }
                for (const Symbol& symbol : symbols_for_document(*imported, false)) {
                    if (symbol.name != imported_name) {
                        continue;
                    }
                    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" +
                           json_escape(symbol.detail) + "`\"}}";
                }
                continue;
            }
            if (import.kind != ImportKind::From || bound_import_name(import) != word) {
                continue;
            }
            const std::optional<Document> imported = imported_document(doc, import);
            if (!imported) {
                continue;
            }
            for (const Symbol& symbol : symbols_for_document(*imported, false)) {
                if (symbol.name != import.imported_name) {
                    continue;
                }
                return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" +
                       json_escape(symbol.detail) + "`\"}}";
            }
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

} // namespace

std::string hover_json(const Document& doc, const std::string& word,
                       const std::string& local_type) {
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol_matches(symbol.name, word)) {
            std::string markdown = "`" + symbol.detail + "`";
            if (uri_for_location(symbol.location, doc) == doc.uri) {
                if (const std::string docs = doc_comment_before(doc, symbol.location.line);
                    !docs.empty()) {
                    markdown += "\n\n" + docs;
                }
            }
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
                   "\"},\"range\":" + range_json(symbol.location) + "}";
        }
    }
    if (const std::optional<std::string> imported = imported_symbol_hover_json(doc, word)) {
        return *imported;
    }
    if (!local_type.empty()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" + json_escape(word) + ": " +
               json_escape(local_type) + "`\"}}";
    }
    return "null";
}

} // namespace dudu
