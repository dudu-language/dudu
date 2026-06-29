#include "dudu/lsp/language_server_symbol_results.hpp"

#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string first_doc_line(std::string_view doc_comment) {
    const size_t newline = doc_comment.find('\n');
    return std::string(doc_comment.substr(0, newline));
}

std::string symbol_detail_with_doc(const Symbol& symbol) {
    if (symbol.doc_comment.empty()) {
        return symbol.detail;
    }
    if (symbol.detail.empty()) {
        return first_doc_line(symbol.doc_comment);
    }
    return symbol.detail + " - " + first_doc_line(symbol.doc_comment);
}

std::string symbol_json(const Symbol& symbol, const std::string& default_uri) {
    const std::string uri = symbol.location.file.empty()
                                ? default_uri
                                : file_uri(std::filesystem::path(symbol.location.file));
    std::ostringstream out;
    out << "{\"name\":\"" << json_escape(symbol.name) << "\",\"kind\":" << symbol.kind
        << ",\"detail\":\"" << json_escape(symbol_detail_with_doc(symbol))
        << "\",\"location\":{\"uri\":\"" << json_escape(uri)
        << "\",\"range\":" << range_json(symbol.location) << "}}";
    return out.str();
}

std::string document_symbol_json(const Symbol& symbol) {
    const std::string range = range_json(symbol.location);
    std::ostringstream out;
    out << "{\"name\":\"" << json_escape(symbol.name) << "\",\"kind\":" << symbol.kind
        << ",\"detail\":\"" << json_escape(symbol_detail_with_doc(symbol))
        << "\",\"range\":" << range << ",\"selectionRange\":" << range << "}";
    return out.str();
}

std::vector<Symbol> document_symbols(const Document& doc, bool include_native) {
    try {
        const ProjectIndex& index = project_index_for_document(doc, include_native);
        return symbols_for_module(index.visible_unit_for_path(doc.path), include_native);
    } catch (const std::exception&) {
    }
    return {};
}

void append_matching_symbols(std::ostringstream& out, bool& first, std::set<std::string>& seen,
                             const std::string& lowered_query, const std::vector<Symbol>& symbols,
                             const std::string& default_uri) {
    for (const Symbol& symbol : symbols) {
        if (!lowered_query.empty() &&
            lower_copy(symbol.name).find(lowered_query) == std::string::npos) {
            continue;
        }
        const std::string uri = symbol.location.file.empty()
                                    ? default_uri
                                    : file_uri(std::filesystem::path(symbol.location.file));
        const std::string key = symbol.name + "\n" + uri + "\n" + range_json(symbol.location);
        if (!seen.insert(key).second) {
            continue;
        }
        if (!first) {
            out << ",";
        }
        first = false;
        out << symbol_json(symbol, default_uri);
    }
}

} // namespace

std::string document_symbols_json(const Document& doc) {
    std::ostringstream out;
    out << "[";
    const std::vector<Symbol> symbols = document_symbols(doc, true);
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << document_symbol_json(symbols[i]);
    }
    out << "]";
    return out.str();
}

std::string workspace_symbols_json(const std::string& query, const ProjectIndex& index,
                                   const Document& seed_doc) {
    const std::string lowered_query = lower_copy(query);
    std::ostringstream out;
    out << "[";
    bool first = true;
    std::set<std::string> seen;
    for (const ProjectModuleSummary& module : index.modules()) {
        const ModuleAst* unit = index.unit_for_module(module.module_path);
        if (unit == nullptr) {
            continue;
        }
        if (same_path(unit->source_path, seed_doc.path)) {
            continue;
        }
        append_matching_symbols(out, first, seen, lowered_query, symbols_for_module(*unit, false),
                                seed_doc.uri);
    }
    const ModuleAst& native_unit = index.visible_unit_for_path(seed_doc.path);
    append_matching_symbols(out, first, seen, lowered_query, symbols_for_module(native_unit, true),
                            seed_doc.uri);
    out << "]";
    return out.str();
}

std::string workspace_symbols_json(const std::string& query,
                                   const std::map<std::string, Document>& open_documents) {
    const std::string lowered_query = lower_copy(query);
    std::ostringstream out;
    out << "[";
    bool first = true;
    std::set<std::string> seen;
    for (const auto& [uri, doc] : open_documents) {
        (void)uri;
        try {
            const ProjectIndex& index = project_index_for_document(doc, true);
            for (const ProjectModuleSummary& module : index.modules()) {
                const ModuleAst* unit = index.unit_for_module(module.module_path);
                if (unit == nullptr) {
                    continue;
                }
                if (same_path(unit->source_path, doc.path)) {
                    continue;
                }
                append_matching_symbols(out, first, seen, lowered_query,
                                        symbols_for_module(*unit, false), doc.uri);
            }
            const ModuleAst& native_unit = index.visible_unit_for_path(doc.path);
            append_matching_symbols(out, first, seen, lowered_query,
                                    symbols_for_module(native_unit, true), doc.uri);
        } catch (const std::exception&) {
        }
    }
    out << "]";
    return out.str();
}

} // namespace dudu
