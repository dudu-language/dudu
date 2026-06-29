#include "dudu/lsp/language_server_references.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_import_references.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_reference_query.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

enum class RenameScope {
    None,
    Workspace,
    CurrentDocument,
};

enum class ReferenceScope {
    None,
    Workspace,
    WorkspaceSkipRedeclarations,
    CurrentDocument,
};

bool renameable_symbol_kind(const int kind) {
    return kind == lsp_symbol_kind::Class || kind == lsp_symbol_kind::Method ||
           kind == lsp_symbol_kind::Field || kind == lsp_symbol_kind::Enum ||
           kind == lsp_symbol_kind::Function || kind == lsp_symbol_kind::Constant;
}

std::string symbol_range_key(const std::string& uri, const std::string& range) {
    return uri + "|" + range;
}

std::string symbol_range_key(const Symbol& symbol, const Document& doc) {
    const int line = std::max(0, symbol.location.line - 1);
    const int column = std::max(0, symbol.location.column - 1);
    return symbol_range_key(
        uri_for_location(symbol.location, doc),
        range_json(line, column, column + static_cast<int>(symbol.name.size())));
}

bool position_contains_name(const Json* params, const std::string& name,
                            const SourceLocation& location) {
    const LspPosition position = lsp_position(params);
    const int target_line = position.line + 1;
    const int target_column = position.character + 1;
    if (location.line != target_line || location.column <= 0) {
        return false;
    }
    const int start = location.column;
    const int end = start + static_cast<int>(name.size());
    return target_column >= start && target_column <= end;
}

std::optional<Symbol> declaration_at_position(const Document& doc, const Json* params,
                                              const std::string& name,
                                              const std::vector<Symbol>& symbols) {
    for (const Symbol& symbol : symbols) {
        if (symbol.name == name && renameable_symbol_kind(symbol.kind) &&
            std::filesystem::path(symbol.location.file) == doc.path &&
            position_contains_name(params, name, symbol.location)) {
            return symbol;
        }
    }
    return std::nullopt;
}

bool document_declares_renameable_symbol(const Document& doc, const std::string& name,
                                         const std::vector<Symbol>& symbols) {
    for (const Symbol& symbol : symbols) {
        if (symbol.name == name && renameable_symbol_kind(symbol.kind) &&
            std::filesystem::path(symbol.location.file) == doc.path) {
            return true;
        }
    }
    return false;
}

std::optional<Symbol>
unique_document_declaration_for_references(const Document& doc, const std::string& name,
                                           const ModuleAst* module,
                                           const std::vector<Symbol>& symbols) {
    if (name.empty() || name.find('.') != std::string::npos || module == nullptr) {
        return std::nullopt;
    }
    std::set<std::string> reference_ranges;
    const std::vector<ReferenceLocation> references = references_in(*module, doc, name);
    for (const ReferenceLocation& location : references) {
        reference_ranges.insert(symbol_range_key(location.uri, location.range));
    }
    std::optional<Symbol> declaration;
    for (const Symbol& symbol : symbols) {
        if (symbol.name != name || !renameable_symbol_kind(symbol.kind)) {
            continue;
        }
        if (!reference_ranges.contains(symbol_range_key(symbol, doc))) {
            continue;
        }
        if (declaration.has_value()) {
            return std::nullopt;
        }
        declaration = symbol;
    }
    return declaration;
}

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

std::optional<std::string> native_identity_for_query(const std::vector<Symbol>& symbols,
                                                     const std::string& query) {
    if (query.empty()) {
        return std::nullopt;
    }
    for (const Symbol& symbol : symbols) {
        if (symbol.name == query && symbol.native_identity_key.has_value()) {
            return symbol.native_identity_key;
        }
    }
    return std::nullopt;
}

std::string dotted_tail(const std::string& query) {
    const size_t dot = query.rfind('.');
    return dot == std::string::npos ? query : query.substr(dot + 1);
}

const ProjectIndex* document_project_index(const Document& doc, bool include_native) {
    try {
        return &project_index_for_document(doc, include_native);
    } catch (const std::exception&) {
        return nullptr;
    }
}

const ModuleAst* visible_document_unit(const ProjectIndex* index, const Document& doc) {
    if (index == nullptr) {
        return nullptr;
    }
    return &index->visible_unit_for_path(doc.path);
}

const ModuleAst* workspace_candidate_unit(const ProjectIndex* workspace_index,
                                          const Document& candidate, bool include_native) {
    if (workspace_index != nullptr) {
        if (const ModuleAst* unit = workspace_index->unit_for_path(candidate.path)) {
            return unit;
        }
    }
    const ProjectIndex* index = document_project_index(candidate, include_native);
    return visible_document_unit(index, candidate);
}

const ProjectIndex* workspace_candidate_index(const ProjectIndex* workspace_index,
                                              const Document& candidate, bool include_native) {
    if (workspace_index != nullptr && workspace_index->unit_for_path(candidate.path) != nullptr) {
        return workspace_index;
    }
    return document_project_index(candidate, include_native);
}

RenameScope rename_scope_at(const Document& doc, const Json* params, const std::string& name,
                            const AstSelection& selection, const ModuleAst* module,
                            const std::vector<Symbol>& symbols_without_native) {
    if (module_import_target_key(doc, name).has_value() ||
        selective_import_target(doc, name).has_value()) {
        return RenameScope::Workspace;
    }
    if (declaration_at_position(doc, params, name, symbols_without_native).has_value()) {
        return RenameScope::Workspace;
    }
    const std::optional<Symbol> declaration =
        unique_document_declaration_for_references(doc, name, module, symbols_without_native);
    if (!declaration.has_value()) {
        return RenameScope::None;
    }
    if (module != nullptr && has_type_ref(local_type_ref_before_cursor(*module, name, params))) {
        return RenameScope::None;
    }
    if (selection.symbol.value_or("") == name && selection.call_callee) {
        return RenameScope::CurrentDocument;
    }
    return RenameScope::None;
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
    if (document_has_structured_references(doc, name, module)) {
        return ReferenceScope::CurrentDocument;
    }
    if (document_has_direct_native_symbol(doc, name, symbols_with_native)) {
        return ReferenceScope::CurrentDocument;
    }
    return ReferenceScope::None;
}

} // namespace

std::string references_json(const Document& doc, const Json* params,
                            const std::map<std::string, Document>& workspace) {
    const ProjectIndex* current_index = document_project_index(doc, true);
    const ModuleAst* current_unit = visible_document_unit(current_index, doc);
    const AstSelection selection =
        current_unit == nullptr ? AstSelection{} : ast_selection_at(*current_unit, params);
    const std::vector<Symbol> current_symbols_with_native =
        current_unit == nullptr ? std::vector<Symbol>{} : symbols_for_module(*current_unit, true);
    const std::vector<Symbol> current_symbols_without_native =
        current_unit == nullptr ? std::vector<Symbol>{} : symbols_for_module(*current_unit, false);
    const std::string query =
        reference_query_at(doc, params, selection, current_unit, current_symbols_with_native);
    const ReferenceScope scope =
        reference_scope_at(doc, params, query, current_unit, current_symbols_without_native,
                           current_symbols_with_native);
    if (scope == ReferenceScope::None) {
        return "[]";
    }
    const std::optional<std::string> module_target =
        scope == ReferenceScope::Workspace ? module_import_target_key(doc, query) : std::nullopt;
    const std::optional<ImportReferenceTarget> selective_target =
        scope == ReferenceScope::Workspace ? selective_import_target(doc, query) : std::nullopt;
    const std::optional<std::string> native_identity =
        native_identity_for_query(current_symbols_with_native, query);
    std::ostringstream out;
    out << "[";
    bool first = true;
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
        std::vector<ReferenceLocation> locations;
        if (scope == ReferenceScope::CurrentDocument && candidate.uri == doc.uri &&
            current_unit != nullptr &&
            has_type_ref(local_type_ref_before_cursor(*current_unit, query, params))) {
            const LspPosition position = lsp_position(params);
            locations = references_in_local_scope(*candidate_unit, candidate, candidate_query,
                                                  position.line + 1)
                            .value_or(std::vector<ReferenceLocation>{});
        } else {
            locations = references_in(*candidate_unit, candidate, candidate_query);
        }
        if (locations.empty()) {
            continue;
        }
        if (!module_target.has_value() && !selective_target.has_value() &&
            native_identity.has_value()) {
            const ProjectIndex* native_candidate_index =
                workspace_candidate_index(current_index, candidate, true);
            if (native_candidate_index == nullptr ||
                !native_candidate_index->module_has_native_identity_for_query(
                    candidate.path, candidate_query, *native_identity)) {
                continue;
            }
        }
        for (const ReferenceLocation& location : locations) {
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
    const ProjectIndex* current_index = document_project_index(doc, true);
    const ModuleAst* current_unit = visible_document_unit(current_index, doc);
    const AstSelection selection =
        current_unit == nullptr ? AstSelection{} : ast_selection_at(*current_unit, params);
    const std::string new_name =
        params == nullptr ? std::string{} : string_value(params->get("newName"));
    const std::vector<Symbol> current_symbols_with_native =
        current_unit == nullptr ? std::vector<Symbol>{} : symbols_for_module(*current_unit, true);
    const std::vector<Symbol> current_symbols_without_native =
        current_unit == nullptr ? std::vector<Symbol>{} : symbols_for_module(*current_unit, false);
    const std::string old_name =
        reference_query_at(doc, params, selection, current_unit, current_symbols_with_native);
    const RenameScope scope = rename_scope_at(doc, params, old_name, selection, current_unit,
                                              current_symbols_without_native);
    if (!valid_identifier(new_name) || scope == RenameScope::None) {
        return "null";
    }
    const std::optional<std::string> module_target =
        scope == RenameScope::Workspace ? module_import_target_key(doc, old_name) : std::nullopt;
    const std::optional<ImportReferenceTarget> selective_target =
        scope == RenameScope::Workspace ? selective_import_target(doc, old_name) : std::nullopt;
    std::ostringstream out;
    out << "{\"changes\":{";
    bool first_doc = true;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        if (scope == RenameScope::CurrentDocument && candidate.uri != doc.uri) {
            continue;
        }
        const ModuleAst* candidate_unit = workspace_candidate_unit(current_index, candidate, false);
        if (candidate_unit == nullptr) {
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
        const std::vector<Symbol> candidate_symbols_without_native =
            symbols_for_module(*candidate_unit, false);
        if (scope == RenameScope::Workspace && candidate.uri != doc.uri &&
            !target_module_document && !target_selective_document &&
            document_declares_renameable_symbol(candidate, old_name,
                                                candidate_symbols_without_native)) {
            continue;
        }
        if (module_target.has_value() && candidate.uri != doc.uri && !target_module_document &&
            module_import_target_key(candidate, old_name) != module_target) {
            continue;
        }
        if (selective_target.has_value() && candidate.uri != doc.uri &&
            !target_selective_document &&
            !same_import_reference_target(selective_import_target(candidate, old_name),
                                          selective_target)) {
            continue;
        }
        const std::string candidate_query = target_module_document ? dotted_tail(old_name)
                                            : target_selective_document
                                                ? selective_target->member_name
                                                : old_name;
        const std::vector<ReferenceLocation> locations =
            references_in(*candidate_unit, candidate, candidate_query);
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
