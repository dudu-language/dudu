#include "dudu/lsp/language_server_references.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_import_references.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_reference_query.hpp"
#include "dudu/lsp/language_server_reference_support.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

enum class ReferenceScope {
    None,
    Workspace,
    WorkspaceSkipRedeclarations,
    CurrentDocument,
};

bool document_has_direct_native_symbol(const Document& doc, const std::string& name,
                                       const std::vector<Symbol>& symbols) {
    if (name.empty() || name.find('.') != std::string::npos) {
        return false;
    }
    for (const Symbol& symbol : symbols) {
        if (symbol.name != name || std::filesystem::path(symbol.location.file) == doc.path) {
            continue;
        }
        if (symbol.kind == lsp_symbol_kind::Class || symbol.kind == lsp_symbol_kind::Struct ||
            symbol.kind == lsp_symbol_kind::Enum || symbol.kind == lsp_symbol_kind::Function ||
            symbol.kind == lsp_symbol_kind::Constant || symbol.kind == lsp_symbol_kind::Namespace) {
            return true;
        }
    }
    return false;
}

bool document_has_structured_references(const Document& doc, const std::string& name,
                                        const ModuleAst* module) {
    if (name.empty() || name.find('.') != std::string::npos || module == nullptr) {
        return false;
    }
    return !references_in(*module, doc, name).empty();
}

bool reference_has_native_identity(const ModuleAst& module, const ReferenceLocation& location,
                                   std::string_view expected_identity) {
    if (location.source_range.start.line <= 0 || location.source_range.start.column <= 0 ||
        location.source_range.end.column <= location.source_range.start.column) {
        return false;
    }
    const LspPosition position{
        .line = location.source_range.start.line - 1,
        .character = location.source_range.end.column - 2,
    };
    const AstSelection selection = ast_selection_at(module, position);
    const std::vector<Symbol> symbols = symbols_for_module(module, true);
    const std::string query =
        selection.symbol_path.value_or(selection.symbol.value_or(std::string{}));
    const std::optional<std::string> identity = native_identity_for_selection(
        selection, &module, symbols, query, location.source_range.start);
    return identity.has_value() && *identity == expected_identity;
}

ReferenceScope reference_scope_at(const Document& doc, const Json* params, const std::string& name,
                                  const ModuleAst* module,
                                  const std::vector<Symbol>& symbols_without_native,
                                  const std::vector<Symbol>& symbols_with_native) {
    if (name.empty()) {
        return ReferenceScope::None;
    }
    if (name.find('.') != std::string::npos) {
        return ReferenceScope::Workspace;
    }
    if (declaration_at_position(doc, params, name, symbols_without_native).has_value()) {
        return ReferenceScope::WorkspaceSkipRedeclarations;
    }
    if (selective_import_target(doc, name).has_value()) {
        return ReferenceScope::Workspace;
    }
    if (unique_document_declaration_for_references(doc, name, module, symbols_without_native)
            .has_value()) {
        return ReferenceScope::CurrentDocument;
    }
    if (native_identity_for_query(symbols_with_native, name).has_value()) {
        return ReferenceScope::Workspace;
    }
    if (document_has_structured_references(doc, name, module)) {
        return ReferenceScope::CurrentDocument;
    }
    if (document_has_direct_native_symbol(doc, name, symbols_with_native)) {
        return ReferenceScope::CurrentDocument;
    }
    return ReferenceScope::None;
}

} // namespace

std::vector<ReferenceLocation> reference_locations(const Document& doc, const Json* params,
                                                   const std::map<std::string, Document>& workspace,
                                                   bool include_native) {
    const ProjectIndex* current_index = document_project_index(doc, include_native);
    const ModuleAst* current_unit = visible_document_unit(current_index, doc);
    if (current_index != nullptr && current_unit != nullptr) {
        if (const std::optional<MacroReferenceTarget> macro =
                macro_reference_target_at(*current_index, *current_unit, params)) {
            std::vector<ReferenceLocation> macro_locations;
            for (const auto& [_, candidate] : workspace) {
                const ProjectIndex* candidate_index =
                    workspace_candidate_index(current_index, candidate, false);
                const ModuleAst* candidate_unit =
                    workspace_candidate_unit(current_index, candidate, false);
                if (candidate_index == nullptr || candidate_unit == nullptr)
                    continue;
                std::vector<ReferenceLocation> found = macro_reference_locations(
                    *candidate_index, *candidate_unit, candidate, macro->identity);
                macro_locations.insert(macro_locations.end(), found.begin(), found.end());
            }
            return macro_locations;
        }
    }
    const AstSelection selection =
        current_unit == nullptr ? AstSelection{} : ast_selection_at(*current_unit, params);
    const std::vector<Symbol> current_symbols_with_native =
        current_unit == nullptr ? std::vector<Symbol>{}
                                : symbols_for_module(*current_unit, include_native);
    const std::vector<Symbol> current_symbols_without_native =
        current_unit == nullptr ? std::vector<Symbol>{} : symbols_for_module(*current_unit, false);
    const std::string query =
        reference_query_at(doc, params, selection, current_unit, current_symbols_with_native);
    const ReferenceScope scope =
        reference_scope_at(doc, params, query, current_unit, current_symbols_without_native,
                           current_symbols_with_native);
    if (scope == ReferenceScope::None) {
        return {};
    }
    const std::optional<std::string> module_target =
        scope == ReferenceScope::Workspace ? module_import_target_key(doc, query) : std::nullopt;
    const std::optional<ImportReferenceTarget> selective_target =
        scope == ReferenceScope::Workspace ? selective_import_target(doc, query) : std::nullopt;
    const std::optional<std::string> native_identity =
        include_native ? native_identity_for_selection(
                             selection, current_unit, current_symbols_with_native, query,
                             SourceLocation{.file = SourceFileName(doc.path.string()),
                                            .line = lsp_position(params).line + 1,
                                            .column = lsp_position(params).character + 1})
                       : std::nullopt;
    const bool filter_native_call_occurrences =
        native_identity.has_value() && selection.call_callee && selection.call_expr.has_value();
    std::vector<ReferenceLocation> out;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        if (scope == ReferenceScope::CurrentDocument && candidate.uri != doc.uri) {
            continue;
        }
        const ModuleAst* candidate_unit = workspace_candidate_unit(current_index, candidate, false);
        if (candidate_unit == nullptr) {
            continue;
        }
        const std::vector<Symbol> candidate_symbols_without_native =
            symbols_for_module(*candidate_unit, false);
        if (scope == ReferenceScope::WorkspaceSkipRedeclarations && candidate.uri != doc.uri &&
            document_declares_renameable_symbol(candidate, query,
                                                candidate_symbols_without_native)) {
            continue;
        }
        std::error_code path_error;
        const std::filesystem::path candidate_path =
            candidate.path.empty() ? std::filesystem::path{}
                                   : std::filesystem::weakly_canonical(candidate.path, path_error);
        const bool target_module_document = module_target.has_value() && !candidate.path.empty() &&
                                            !path_error &&
                                            same_path(candidate_path, *module_target);
        const bool target_selective_document =
            selective_target.has_value() && !candidate.path.empty() && !path_error &&
            same_path(candidate_path, selective_target->source_key);
        if (module_target.has_value() && candidate.uri != doc.uri && !target_module_document &&
            module_import_target_key(candidate, query) != module_target) {
            continue;
        }
        if (selective_target.has_value() && candidate.uri != doc.uri &&
            !target_selective_document &&
            !same_import_reference_target(selective_import_target(candidate, query),
                                          selective_target)) {
            continue;
        }
        const std::string candidate_query = target_module_document ? dotted_tail(query)
                                            : target_selective_document
                                                ? selective_target->member_name
                                                : query;
        std::vector<std::string> candidate_queries = {candidate_query};
        if (!module_target.has_value() && !selective_target.has_value() &&
            native_identity.has_value()) {
            const std::string symbol_segment = dotted_tail(query);
            if (candidate.uri != doc.uri &&
                !module_may_reference_name_segment(*candidate_unit, symbol_segment)) {
                continue;
            }
            const ProjectIndex* native_candidate_index =
                workspace_candidate_index(current_index, candidate, include_native);
            if (native_candidate_index == nullptr) {
                continue;
            }
            candidate_unit = &native_candidate_index->visible_unit_for_path(candidate.path);
            const std::set<std::string> identity_queries =
                native_candidate_index->native_queries_for_identity(candidate.path,
                                                                    *native_identity);
            if (identity_queries.empty()) {
                continue;
            }
            candidate_queries.assign(identity_queries.begin(), identity_queries.end());
        }
        std::vector<ReferenceLocation> locations;
        if (scope == ReferenceScope::CurrentDocument && candidate.uri == doc.uri &&
            current_unit != nullptr &&
            has_type_ref(local_type_ref_before_cursor(*current_unit, query, params))) {
            const LspPosition position = lsp_position(params);
            locations = references_in_local_scope(*candidate_unit, candidate, candidate_query,
                                                  position.line + 1)
                            .value_or(std::vector<ReferenceLocation>{});
        } else {
            for (const std::string& identity_query : candidate_queries) {
                std::vector<ReferenceLocation> found =
                    references_in(*candidate_unit, candidate, identity_query);
                if (filter_native_call_occurrences) {
                    std::erase_if(found, [&](const ReferenceLocation& location) {
                        return !reference_has_native_identity(*candidate_unit, location,
                                                              *native_identity);
                    });
                }
                locations.insert(locations.end(), found.begin(), found.end());
            }
        }
        if (locations.empty()) {
            continue;
        }
        for (const ReferenceLocation& location : locations) {
            out.push_back(location);
        }
    }
    return out;
}

std::string references_json(const Document& doc, const Json* params,
                            const std::map<std::string, Document>& workspace) {
    const std::vector<ReferenceLocation> locations =
        reference_locations(doc, params, workspace, true);
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

} // namespace dudu
