#include "dudu/lsp/language_server_code_lens.hpp"

#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

bool code_lens_symbol_kind(int kind) {
    return kind == lsp_symbol_kind::Class || kind == lsp_symbol_kind::Enum ||
           kind == lsp_symbol_kind::Function || kind == lsp_symbol_kind::Method ||
           kind == lsp_symbol_kind::Constant;
}

bool same_document_symbol(const Symbol& symbol, const Document& doc) {
    return !symbol.location.file.empty() && std::filesystem::path(symbol.location.file) == doc.path;
}

std::string text_document_position_params(const Document& doc, const SourceLocation& location) {
    const int line = std::max(0, location.line - 1);
    const int character = std::max(0, location.column - 1);
    std::ostringstream out;
    out << "{\"textDocument\":{\"uri\":\"" << json_escape(doc.uri)
        << "\"},\"position\":{\"line\":" << line << ",\"character\":" << character << "}}";
    return out.str();
}

std::string code_lens_location_array(const std::vector<ReferenceLocation>& locations) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < locations.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << location_json(locations[i].uri, locations[i].range);
    }
    out << "]";
    return out.str();
}

std::string code_lens_json_for_symbol(const Document& doc, const Symbol& symbol,
                                      const std::vector<ReferenceLocation>& locations) {
    const int line = std::max(0, symbol.location.line - 1);
    const int character = std::max(0, symbol.location.column - 1);
    const std::string title =
        locations.size() == 1 ? "1 reference" : std::to_string(locations.size()) + " references";
    std::ostringstream out;
    out << "{\"range\":"
        << range_json(line, character, character + static_cast<int>(symbol.name.size()))
        << ",\"command\":{\"title\":\"" << title
        << "\",\"command\":\"dudu.showReferences\",\"arguments\":[\"" << json_escape(doc.uri)
        << "\",{\"line\":" << line << ",\"character\":" << character << "},"
        << code_lens_location_array(locations) << "]}}";
    return out.str();
}

} // namespace

std::string code_lens_json(const Document& doc, const Json*,
                           const std::map<std::string, Document>& workspace) {
    const ProjectIndex& index = project_index_for_document(doc, false);
    const ModuleAst& module = index.visible_unit_for_path(doc.path);
    const std::vector<Symbol> symbols = symbols_for_module(module, false);
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const Symbol& symbol : symbols) {
        if (!code_lens_symbol_kind(symbol.kind) || !same_document_symbol(symbol, doc)) {
            continue;
        }
        Json params = JsonParser(text_document_position_params(doc, symbol.location)).parse();
        const std::vector<ReferenceLocation> locations =
            reference_locations(doc, &params, workspace, false);
        if (locations.empty()) {
            continue;
        }
        if (!first) {
            out << ",";
        }
        first = false;
        out << code_lens_json_for_symbol(doc, symbol, locations);
    }
    out << "]";
    return out.str();
}

} // namespace dudu
