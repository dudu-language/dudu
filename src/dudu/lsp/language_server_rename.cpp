#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_generic_params.hpp"
#include "dudu/lsp/language_server_import_references.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_member_references.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_reference_query.hpp"
#include "dudu/lsp/language_server_reference_support.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

std::optional<std::string> prepare_rename_range_json(const Document& doc, const Json* params,
                                                     const std::string& query) {
    const LspPosition position = lsp_position(params);
    const LexResult lexed = lex_source_recovering(doc.text, doc.path);
    for (const Token& token : lexed.tokens) {
        if (token.kind != TokenKind::Identifier || token.location.line != position.line + 1) {
            continue;
        }
        const SourceLocation end = token_end_location(token);
        const int start_character = token.location.column - 1;
        const int end_character = end.column - 1;
        if (position.character < start_character || position.character > end_character ||
            token.text != dotted_tail(query)) {
            continue;
        }
        return "{\"range\":" +
               range_json(position.line, start_character, position.line, end_character) +
               ",\"placeholder\":\"" + json_escape(token.text) + "\"}";
    }
    return std::nullopt;
}

bool dudu_member_target(const std::optional<MemberReferenceTarget>& target) {
    return target.has_value() && !target->declaration.file.empty() &&
           std::filesystem::path(target->declaration.file.str()).extension() == ".dd";
}

std::string workspace_edit_json(const std::vector<ReferenceLocation>& locations,
                                const std::string& new_name) {
    std::map<std::string, std::vector<const ReferenceLocation*>> by_uri;
    for (const ReferenceLocation& location : locations) {
        by_uri[location.uri].push_back(&location);
    }
    std::ostringstream out;
    out << "{\"changes\":{";
    bool first_document = true;
    for (const auto& [uri, document_locations] : by_uri) {
        if (!first_document) {
            out << ",";
        }
        first_document = false;
        out << "\"" << json_escape(uri) << "\":[";
        for (size_t index = 0; index < document_locations.size(); ++index) {
            if (index > 0) {
                out << ",";
            }
            out << "{\"range\":" << document_locations[index]->range << ",\"newText\":\""
                << json_escape(new_name) << "\"}";
        }
        out << "]";
    }
    out << "}}";
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
    const std::optional<GenericParamTarget> generic =
        current_unit == nullptr
            ? std::nullopt
            : generic_param_target_at(*current_unit, lsp_position(params),
                                      selection.symbol.value_or(dotted_tail(old_name)));
    const std::optional<MemberReferenceTarget> member =
        member_reference_target_at(doc, params, selection, current_unit);
    const bool local = current_unit != nullptr && old_name != "self" && old_name != "super" &&
                       old_name.find('.') == std::string::npos &&
                       has_type_ref(local_type_ref_before_cursor(*current_unit, old_name, params));
    if (generic.has_value() || dudu_member_target(member) || local) {
        if (!valid_identifier(new_name)) {
            return "null";
        }
        return workspace_edit_json(reference_locations(doc, params, workspace, false), new_name);
    }
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
        return "null";
    }
    const AstSelection selection = ast_selection_at(*current_unit, params);
    const std::vector<Symbol> current_symbols_without_native =
        symbols_for_module(*current_unit, false);
    const std::string old_name =
        reference_query_at(doc, params, selection, current_unit, current_symbols_without_native);
    const std::optional<GenericParamTarget> generic = generic_param_target_at(
        *current_unit, lsp_position(params), selection.symbol.value_or(dotted_tail(old_name)));
    const std::optional<MemberReferenceTarget> member =
        member_reference_target_at(doc, params, selection, current_unit);
    const bool local = old_name != "self" && old_name != "super" &&
                       old_name.find('.') == std::string::npos &&
                       has_type_ref(local_type_ref_before_cursor(*current_unit, old_name, params));
    if (generic.has_value() || dudu_member_target(member) || local) {
        const std::optional<std::string> range = prepare_rename_range_json(doc, params, old_name);
        return range.value_or("null");
    }
    const RenameScope scope = rename_scope_at(doc, params, old_name, selection, current_unit,
                                              current_symbols_without_native);
    if (scope == RenameScope::None) {
        return "null";
    }
    const std::optional<std::string> range = prepare_rename_range_json(doc, params, old_name);
    return range.value_or("null");
}

} // namespace dudu
