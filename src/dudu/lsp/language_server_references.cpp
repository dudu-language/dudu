#include "dudu/lsp/language_server_references.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_import_references.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_reference_query.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
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
        if (symbol.location.file.ends_with(".dd")) {
            continue;
        }
        if (symbol.name == query && symbol.native_identity_key.has_value()) {
            return symbol.native_identity_key;
        }
    }
    return std::nullopt;
}

std::optional<std::string> native_identity_for_selection(const AstSelection& selection,
                                                         const ModuleAst* module,
                                                         const std::vector<Symbol>& symbols,
                                                         const std::string& query,
                                                         const SourceLocation& cursor_location) {
    if (module != nullptr && selection.call_callee && selection.call_expr.has_value()) {
        try {
            Symbols sema_symbols = collect_symbols(*module);
            FunctionScope scope(sema_symbols);
            scope.local_type_refs = local_type_refs_before_location(*module, cursor_location);
            const Expr& call = *selection.call_expr;
            const std::vector<TypeRef> explicit_args = call.kind == ExprKind::TemplateCall
                                                           ? template_type_refs(call)
                                                           : std::vector<TypeRef>{};
            if (const std::optional<NativeSignatureMatch> matched =
                    match_native_signature_declaration(scope, query, explicit_args, call.children,
                                                       nullptr);
                matched && matched->declaration != nullptr) {
                if (std::filesystem::path(matched->declaration->location.file).extension() ==
                    ".dd") {
                    return std::nullopt;
                }
                const std::string identity =
                    native_symbol_identity_key(matched->declaration->identity);
                if (!identity.empty()) {
                    return identity;
                }
            }
            if (has_expr_callee(call) && expr_callee(call).front().kind == ExprKind::Member &&
                expr_callee(call).front().children.size() == 1) {
                const Expr& member = expr_callee(call).front();
                const std::optional<TypeRef> static_receiver =
                    static_class_receiver_type_ref(scope, member.children.front());
                const TypeRef receiver = static_receiver.value_or(
                    infer_expr_type_ast(scope, member.children.front(), nullptr));
                const std::vector<DuduMethodInstantiation> methods =
                    static_receiver
                        ? dudu_static_method_instantiations_for_type(scope.symbols, receiver,
                                                                     member.name, explicit_args)
                        : dudu_method_instantiations_for_type(scope.symbols, receiver, member.name,
                                                              explicit_args);
                if (!methods.empty()) {
                    std::vector<FunctionSignature> signatures;
                    signatures.reserve(methods.size());
                    for (const DuduMethodInstantiation& method : methods) {
                        signatures.push_back(method.signature);
                    }
                    if (const std::optional<size_t> matched =
                            matching_signature_index_ast(scope, signatures, call.children);
                        matched && methods[*matched].method != nullptr &&
                        std::filesystem::path(methods[*matched].method->location.file)
                                .extension() != ".dd") {
                        const std::string identity =
                            native_symbol_identity_key(methods[*matched].method->native_identity);
                        return identity.empty() ? std::nullopt
                                                : std::optional<std::string>{identity};
                    }
                    return std::nullopt;
                }
            }
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }
    return native_identity_for_query(symbols, query);
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

std::string rename_json(const Document& doc, const Json* params,
                        const std::map<std::string, Document>& workspace) {
    const ProjectIndex* current_index = document_project_index(doc, true);
    const ModuleAst* current_unit = visible_document_unit(current_index, doc);
    const std::string new_name =
        params == nullptr ? std::string{} : string_value(params->get("newName"));
    if (current_index != nullptr && current_unit != nullptr) {
        if (const std::optional<MacroReferenceTarget> macro =
                macro_reference_target_at(*current_index, *current_unit, params)) {
            if (!valid_identifier(new_name))
                return "null";
            std::ostringstream macro_edit;
            macro_edit << "{\"changes\":{";
            bool first_document = true;
            for (const auto& [_, candidate] : workspace) {
                const ProjectIndex* candidate_index =
                    workspace_candidate_index(current_index, candidate, false);
                const ModuleAst* candidate_unit =
                    workspace_candidate_unit(current_index, candidate, false);
                if (candidate_index == nullptr || candidate_unit == nullptr)
                    continue;
                const std::vector<ReferenceLocation> locations = macro_reference_locations(
                    *candidate_index, *candidate_unit, candidate, macro->identity);
                if (locations.empty())
                    continue;
                if (!first_document)
                    macro_edit << ",";
                first_document = false;
                macro_edit << "\"" << json_escape(candidate.uri) << "\":[";
                for (size_t i = 0; i < locations.size(); ++i) {
                    if (i > 0)
                        macro_edit << ",";
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

std::string prepare_rename_json(const Document& doc, const Json* params) {
    const ProjectIndex* current_index = document_project_index(doc, false);
    const ModuleAst* current_unit = visible_document_unit(current_index, doc);
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
            while (start > 0 && identifier_char((*line)[static_cast<size_t>(start - 1)]))
                --start;
            int end = std::max(0, position.character);
            while (end < static_cast<int>(line->size()) &&
                   identifier_char((*line)[static_cast<size_t>(end)]))
                ++end;
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
