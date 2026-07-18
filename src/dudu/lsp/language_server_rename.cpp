#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_import_references.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_reference_query.hpp"
#include "dudu/lsp/language_server_reference_support.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

enum class RenameScope {
    None,
    Workspace,
    CurrentDocument,
};

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

std::optional<std::string_view> line_at(const std::string& text, int line) {
    if (line < 0) {
        return std::nullopt;
    }
    size_t start = 0;
    int current_line = 0;
    while (current_line < line) {
        const size_t next = text.find('\n', start);
        if (next == std::string::npos) {
            return std::nullopt;
        }
        start = next + 1;
        ++current_line;
    }
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
        end = text.size();
    }
    if (end > start && text[end - 1] == '\r') {
        --end;
    }
    return std::string_view(text).substr(start, end - start);
}

std::optional<std::string> prepare_rename_range_json(const Document& doc, const Json* params,
                                                     const std::string& query) {
    const LspPosition position = lsp_position(params);
    const std::optional<std::string_view> line = line_at(doc.text, position.line);
    if (!line || position.character < 0 || position.character > static_cast<int>(line->size())) {
        return std::nullopt;
    }
    int start = position.character;
    if (start == static_cast<int>(line->size()) && start > 0 &&
        identifier_char((*line)[static_cast<size_t>(start - 1)])) {
        --start;
    }
    if (start < 0 || start >= static_cast<int>(line->size()) ||
        !identifier_char((*line)[static_cast<size_t>(start)])) {
        return std::nullopt;
    }
    int end = start;
    while (start > 0 && identifier_char((*line)[static_cast<size_t>(start - 1)])) {
        --start;
    }
    while (end < static_cast<int>(line->size()) &&
           identifier_char((*line)[static_cast<size_t>(end)])) {
        ++end;
    }
    const std::string placeholder =
        std::string(line->substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
    if (placeholder.empty() || !valid_identifier(placeholder) ||
        placeholder != dotted_tail(query)) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "{\"range\":" << range_json(position.line, start, position.line, end)
        << ",\"placeholder\":\"" << json_escape(placeholder) << "\"}";
    return out.str();
}

} // namespace

std::string rename_json(const Document& doc, const Json* params,
                        const std::map<std::string, Document>& workspace) {
    const ProjectIndexSnapshot current_index = document_project_index(doc, true);
    const ModuleAst* current_unit = visible_document_unit(current_index.get(), doc);
    const std::string new_name =
        params == nullptr ? std::string{} : string_value(params->get("newName"));
    if (current_index != nullptr && current_unit != nullptr) {
        if (const std::optional<MacroReferenceTarget> macro =
                macro_reference_target_at(*current_index, *current_unit, params)) {
            if (!valid_identifier(new_name)) {
                return "null";
            }
            std::ostringstream macro_edit;
            macro_edit << "{\"changes\":{";
            bool first_document = true;
            for (const auto& [_, candidate] : workspace) {
                const ProjectIndexSnapshot candidate_index =
                    workspace_candidate_index(current_index, candidate, false);
                const ModuleAst* candidate_unit =
                    visible_document_unit(candidate_index.get(), candidate);
                if (candidate_index == nullptr || candidate_unit == nullptr) {
                    continue;
                }
                const std::vector<ReferenceLocation> locations = macro_reference_locations(
                    *candidate_index, *candidate_unit, candidate, macro->identity);
                if (locations.empty()) {
                    continue;
                }
                if (!first_document) {
                    macro_edit << ",";
                }
                first_document = false;
                macro_edit << "\"" << json_escape(candidate.uri) << "\":[";
                for (size_t i = 0; i < locations.size(); ++i) {
                    if (i > 0) {
                        macro_edit << ",";
                    }
                    macro_edit << "{\"range\":" << locations[i].range << ",\"newText\":\""
                               << json_escape(new_name) << "\"}";
                }
                macro_edit << "]";
            }
            macro_edit << "}}";
            return macro_edit.str();
        }
    }
    const AstSelection selection =
        current_unit == nullptr ? AstSelection{} : ast_selection_at(*current_unit, params);
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
        const ProjectIndexSnapshot candidate_index =
            workspace_candidate_index(current_index, candidate, false);
        const ModuleAst* candidate_unit = visible_document_unit(candidate_index.get(), candidate);
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

std::string prepare_rename_json(const Document& doc, const Json* params) {
    const ProjectIndexSnapshot current_index = document_project_index(doc, false);
    const ModuleAst* current_unit = visible_document_unit(current_index.get(), doc);
    if (current_unit == nullptr) {
        return "null";
    }
    if (current_index != nullptr &&
        macro_reference_target_at(*current_index, *current_unit, params).has_value()) {
        const AstSelection macro_selection = ast_selection_at(*current_unit, params);
        const std::string query =
            macro_selection.symbol.value_or(macro_selection.symbol_path.value_or(std::string{}));
        if (const std::optional<std::string> range =
                prepare_rename_range_json(doc, params, query)) {
            return *range;
        }
        const LspPosition position = lsp_position(params);
        const std::optional<std::string_view> line = line_at(doc.text, position.line);
        if (line) {
            int start = std::min(position.character, static_cast<int>(line->size()));
            while (start > 0 && identifier_char((*line)[static_cast<size_t>(start - 1)])) {
                --start;
            }
            int end = std::max(0, position.character);
            while (end < static_cast<int>(line->size()) &&
                   identifier_char((*line)[static_cast<size_t>(end)])) {
                ++end;
            }
            if (end > start) {
                const std::string placeholder = std::string(
                    line->substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
                return "{\"range\":" + range_json(position.line, start, position.line, end) +
                       ",\"placeholder\":\"" + json_escape(placeholder) + "\"}";
            }
        }
        return "null";
    }
    const AstSelection selection = ast_selection_at(*current_unit, params);
    const std::vector<Symbol> current_symbols_without_native =
        symbols_for_module(*current_unit, false);
    const std::string old_name =
        reference_query_at(doc, params, selection, current_unit, current_symbols_without_native);
    const RenameScope scope = rename_scope_at(doc, params, old_name, selection, current_unit,
                                              current_symbols_without_native);
    if (scope == RenameScope::None) {
        return "null";
    }
    const std::optional<std::string> range = prepare_rename_range_json(doc, params, old_name);
    return range.value_or("null");
}

} // namespace dudu
