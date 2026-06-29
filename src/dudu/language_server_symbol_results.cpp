#include "dudu/language_server_symbol_results.hpp"

#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string symbol_json(const Symbol& symbol, const Document& doc) {
    std::ostringstream out;
    out << "{\"name\":\"" << json_escape(symbol.name) << "\",\"kind\":" << symbol.kind
        << ",\"detail\":\"" << json_escape(symbol.detail) << "\",\"location\":{\"uri\":\""
        << json_escape(uri_for_location(symbol.location, doc))
        << "\",\"range\":" << range_json(symbol.location) << "}}";
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

} // namespace

std::string document_symbols_json(const Document& doc) {
    std::ostringstream out;
    out << "[";
    const std::vector<Symbol> symbols = document_symbols(doc, true);
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << symbol_json(symbols[i], doc);
    }
    out << "]";
    return out.str();
}

std::string workspace_symbols_json(const std::string& query,
                                   const std::map<std::string, Document>& workspace,
                                   const std::map<std::string, Document>& open_documents) {
    const std::string lowered_query = lower_copy(query);
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& [uri, doc] : workspace) {
        const bool include_native = open_documents.contains(uri);
        for (const Symbol& symbol : document_symbols(doc, include_native)) {
            if (!lowered_query.empty() &&
                lower_copy(symbol.name).find(lowered_query) == std::string::npos) {
                continue;
            }
            if (!first) {
                out << ",";
            }
            first = false;
            out << symbol_json(symbol, doc);
        }
    }
    out << "]";
    return out.str();
}

} // namespace dudu
