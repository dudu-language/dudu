#include "dudu/language_server_hover.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_native_lookup.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/module_names.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <filesystem>
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

std::optional<std::string> imported_symbol_hover_json(const Document& doc,
                                                      const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    try {
        const ModuleAst module = module_for_document(doc, false);
        const ModuleAst& current = visible_module_unit(module, doc.path);
        for (const ImportDecl& import : current.imports) {
            std::string target;
            if (import.kind == ImportKind::Module) {
                const std::string bound = bound_import_name(import);
                const std::vector<std::string> prefixes =
                    import.alias.empty() ? std::vector<std::string>{import.module_path, bound}
                                         : std::vector<std::string>{bound};
                for (const std::string& prefix : prefixes) {
                    if (word.rfind(prefix + ".", 0) == 0) {
                        target = word.substr(prefix.size() + 1);
                        break;
                    }
                }
                if (target.empty()) {
                    continue;
                }
            } else if (import.kind == ImportKind::From && bound_import_name(import) == word) {
                target = import.imported_name;
            } else {
                continue;
            }
            const std::filesystem::path file =
                module_path_to_file(doc.path.parent_path(), import.module_path);
            std::error_code error;
            if (!std::filesystem::exists(file, error) || error) {
                continue;
            }
            const ModuleAst* imported = imported_module_unit(module, current, import);
            ModuleAst loaded_imported;
            if (imported == nullptr) {
                loaded_imported = load_source_tree(file);
                imported = &visible_module_unit(loaded_imported, file);
            }
            for (const Symbol& symbol : symbols_for_module(*imported, false)) {
                if (symbol.name != target) {
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

std::optional<std::string> native_alias_hover_json(const Document& doc, const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    try {
        ModuleAst module = module_for_document(doc, true);
        const auto build_hover = [&](const NativeTypeDecl& type) -> std::optional<std::string> {
            const std::optional<NativeClassDefinition> target =
                native_alias_target_class_definition(module, type);
            if (!target) {
                return std::nullopt;
            }
            const std::string markdown = "`native type = " + native_type_alias_type_text(type) +
                                         "`\n\nresolves to `" + "native class " + target->name +
                                         "`";
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
                   "\"},\"range\":" + range_json(type.location) + "}";
        };
        for (const NativeTypeDecl& type : module.native_types) {
            if (type.name == word) {
                return build_hover(type);
            }
        }
        for (const NativeTypeDecl& type : module.native_types) {
            if (symbol_matches(type.name, word)) {
                return build_hover(type);
            }
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::string symbol_hover_json(const Document& doc, const Symbol& symbol) {
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

} // namespace

std::string hover_json(const Document& doc, const std::string& word,
                       const std::string& local_type) {
    if (const std::optional<std::string> native_alias = native_alias_hover_json(doc, word)) {
        return *native_alias;
    }
    const std::vector<Symbol> symbols = symbols_for_document(doc);
    if (const std::optional<Symbol> exact = exact_symbol_match(symbols, word)) {
        return symbol_hover_json(doc, *exact);
    }
    if (const std::optional<std::string> imported = imported_symbol_hover_json(doc, word)) {
        return *imported;
    }
    if (const std::optional<Symbol> suffix = unambiguous_suffix_symbol_match(symbols, word)) {
        return symbol_hover_json(doc, *suffix);
    }
    if (!local_type.empty()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" + json_escape(word) + ": " +
               json_escape(local_type) + "`\"}}";
    }
    return "null";
}

} // namespace dudu
