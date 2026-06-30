#include "dudu/lsp/language_server_declaration_references.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

bool declaration_symbol_kind(int kind) {
    return kind == lsp_symbol_kind::Class || kind == lsp_symbol_kind::Struct ||
           kind == lsp_symbol_kind::Enum || kind == lsp_symbol_kind::Function ||
           kind == lsp_symbol_kind::Method || kind == lsp_symbol_kind::Constant ||
           kind == lsp_symbol_kind::Field;
}

std::string symbol_name_range_json(const Symbol& symbol) {
    const int line = std::max(0, symbol.location.line - 1);
    const int character = std::max(0, symbol.location.column - 1);
    return range_json(line, character, character + static_cast<int>(symbol.name.size()));
}

bool declaration_symbol_at_position(const Symbol& symbol, const Document& doc,
                                    const LspPosition& position) {
    if (!declaration_symbol_kind(symbol.kind) || symbol.location.file.empty()) {
        return false;
    }
    std::error_code error;
    if (!same_path(std::filesystem::weakly_canonical(symbol.location.file.str(), error),
                   doc.path)) {
        return false;
    }
    const int line = std::max(0, symbol.location.line - 1);
    const int start = std::max(0, symbol.location.column - 1);
    const int end = start + static_cast<int>(symbol.name.size());
    return position.line == line && start <= position.character && position.character <= end;
}

std::string reference_locations_json(const std::vector<ReferenceLocation>& locations) {
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

bool same_source_path(const std::filesystem::path& lhs, const SourceFileName& rhs) {
    return !rhs.empty() && same_path(lhs, std::filesystem::path(rhs.str()));
}

bool same_imported_module_file(const ModuleAst& imported, const Symbol& symbol) {
    if (!imported.source_path.empty() &&
        same_source_path(imported.source_path, symbol.location.file)) {
        return true;
    }
    return !imported.module_path.empty() &&
           imported.module_path ==
               std::filesystem::path(symbol.location.file.str()).stem().string();
}

void append_unique_locations(std::vector<ReferenceLocation>& out,
                             const std::vector<ReferenceLocation>& extra) {
    std::set<std::string> seen;
    for (const ReferenceLocation& location : out) {
        seen.insert(location.uri + "|" + location.range);
    }
    for (const ReferenceLocation& location : extra) {
        if (seen.insert(location.uri + "|" + location.range).second) {
            out.push_back(location);
        }
    }
}

void append_import_alias_references(const Symbol& symbol,
                                    const std::map<std::string, Document>& workspace,
                                    std::vector<ReferenceLocation>& locations) {
    if (symbol.name.empty() || symbol.location.file.empty()) {
        return;
    }
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        try {
            const ProjectIndex& index = project_index_for_document(candidate, false);
            const ModuleAst& module = index.visible_unit_for_path(candidate.path);
            for (const ImportDecl& import : module.imports) {
                if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
                    continue;
                }
                const ModuleAst* imported = index.imported_unit(module, import);
                if (imported == nullptr || !same_imported_module_file(*imported, symbol)) {
                    continue;
                }
                if (import.kind == ImportKind::Module) {
                    append_unique_locations(
                        locations, references_in(module, candidate,
                                                 bound_import_name(import) + "." + symbol.name));
                } else if (import.imported_name == symbol.name) {
                    append_unique_locations(
                        locations, references_in(module, candidate, bound_import_name(import)));
                }
            }
        } catch (const std::exception&) {
        }
    }
}

} // namespace

std::optional<std::string>
declaration_references_json(const Document& doc, const ModuleAst& current, const Json* params,
                            const std::map<std::string, Document>& workspace) {
    if (workspace.empty()) {
        return std::nullopt;
    }
    const LspPosition position = lsp_position(params);
    for (const Symbol& symbol : symbols_for_module(current, false)) {
        if (!declaration_symbol_at_position(symbol, doc, position)) {
            continue;
        }
        std::vector<ReferenceLocation> locations =
            reference_locations(doc, params, workspace, false);
        append_import_alias_references(symbol, workspace, locations);
        const std::string declaration_uri = uri_for_location(symbol.location, doc);
        const std::string declaration_range = symbol_name_range_json(symbol);
        locations.erase(std::remove_if(locations.begin(), locations.end(),
                                       [&](const ReferenceLocation& location) {
                                           return location.uri == declaration_uri &&
                                                  location.range == declaration_range;
                                       }),
                        locations.end());
        return locations.empty() ? std::optional<std::string>{"null"}
                                 : std::optional<std::string>{reference_locations_json(locations)};
    }
    return std::nullopt;
}

} // namespace dudu
