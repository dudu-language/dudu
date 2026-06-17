#include "dudu/language_server_references.hpp"

#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_symbols.hpp"

#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

bool renameable_symbol(const Document& doc, const std::string& name) {
    if (name.empty() || name.find('.') != std::string::npos) {
        return false;
    }
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol.name == name && std::filesystem::path(symbol.location.file) == doc.path &&
            (symbol.kind == 5 || symbol.kind == 6 || symbol.kind == 8 || symbol.kind == 10 ||
             symbol.kind == 12 || symbol.kind == 14)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string references_json(const Document& doc, const Json* params,
                            const std::map<std::string, Document>& workspace) {
    const std::string query = ast_symbol_at(doc, params).value_or("");
    if (query.empty()) {
        return "[]";
    }
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        for (const ReferenceLocation& location : references_in(candidate, query)) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << location_json(location.uri, location.range);
        }
    }
    out << "]";
    return out.str();
}

std::string rename_json(const Document& doc, const Json* params,
                        const std::map<std::string, Document>& workspace) {
    const std::string old_name = ast_symbol_at(doc, params).value_or("");
    const std::string new_name =
        params == nullptr ? std::string{} : string_value(params->get("newName"));
    if (!valid_identifier(new_name) || !renameable_symbol(doc, old_name)) {
        return "null";
    }
    std::ostringstream out;
    out << "{\"changes\":{";
    bool first_doc = true;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        const std::vector<ReferenceLocation> locations = references_in(candidate, old_name);
        if (locations.empty()) {
            continue;
        }
        if (!first_doc) {
            out << ",";
        }
        first_doc = false;
        out << "\"" << json_escape(candidate.uri) << "\":[";
        for (size_t i = 0; i < locations.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << "{\"range\":" << locations[i].range << ",\"newText\":\"" << json_escape(new_name)
                << "\"}";
        }
        out << "]";
    }
    out << "}}";
    return out.str();
}

} // namespace dudu
