#include "dudu/language_server_references.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/language_server_import_references.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_local_context.hpp"
#include "dudu/language_server_native_lookup.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_reference_collect.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/project_index.hpp"

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
    if (name.empty() || name.find('.') != std::string::npos) {
        return std::nullopt;
    }
    std::set<std::string> reference_ranges;
    const std::vector<ReferenceLocation> references =
        module == nullptr ? references_in(doc, name) : references_in(*module, doc, name);
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

bool document_has_type_symbol(const std::vector<Symbol>& symbols, const std::string& name) {
    for (const Symbol& symbol : symbols) {
        if (symbol.name == name &&
            (symbol.kind == lsp_symbol_kind::Class || symbol.kind == lsp_symbol_kind::Struct)) {
            return true;
        }
    }
    return false;
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

bool document_has_native_identity_for_query(const std::vector<Symbol>& symbols,
                                            const std::string& query, const std::string& identity) {
    if (identity.empty()) {
        return true;
    }
    for (const Symbol& symbol : symbols) {
        if (symbol.name == query && symbol.native_identity_key == identity) {
            return true;
        }
    }
    return false;
}

bool dudu_module_identity(const std::string& identity) {
    constexpr std::string_view prefix = "path:";
    return identity.rfind(prefix, 0) == 0;
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

std::string reference_query_at(const Document& doc, const AstSelection& selection,
                               const ModuleAst* module,
                               const std::vector<Symbol>& symbols_with_native) {
    const std::string name = selection.symbol.value_or("");
    std::optional<std::string> expression_path;
    std::vector<std::string> paths;
    if (selection.symbol_path.has_value()) {
        paths.push_back(*selection.symbol_path);
    }
    if (selection.expr_path.has_value()) {
        expression_path = render_expr_path(*selection.expr_path);
        paths.push_back(*expression_path);
    }
    if (module != nullptr) {
        for (const std::string& path : paths) {
            if (path.empty() || path == name || path.find('.') == std::string::npos) {
                continue;
            }
            if (native_alias_target_class_definition(*module, path).has_value()) {
                return path;
            }
            if (module_import_target_key(doc, path).has_value()) {
                return path;
            }
            for (const ClassDecl& klass : module->native_classes) {
                if (klass.name == path) {
                    return path;
                }
            }
            for (const NativeFunctionDecl& fn : module->native_functions) {
                if (fn.name == path) {
                    return path;
                }
            }
            for (const NativeValueDecl& value : module->native_values) {
                if (value.name == path) {
                    return path;
                }
            }
            for (const NativeMacroDecl& macro : module->native_macros) {
                if (macro.name == path) {
                    return path;
                }
            }
            if (document_has_type_symbol(symbols_with_native, path)) {
                return path;
            }
        }
    }
    if (expression_path.has_value() && expression_path->find('.') != std::string::npos) {
        return *expression_path;
    }
    return name;
}

RenameScope rename_scope_at(const Document& doc, const Json* params, const std::string& name,
                            const AstSelection& selection, const ModuleAst* module,
                            const std::vector<Symbol>& symbols_without_native) {
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
                                  const AstSelection& selection, const ModuleAst* module,
                                  const std::vector<Symbol>& symbols_without_native,
                                  const std::vector<Symbol>& symbols_with_native) {
    if (name.empty()) {
        return ReferenceScope::None;
    }
    if (declaration_at_position(doc, params, name, symbols_without_native).has_value()) {
        return ReferenceScope::WorkspaceSkipRedeclarations;
    }
    if (selection.symbol_path.has_value() &&
        selection.symbol_path->find('.') != std::string::npos) {
        return ReferenceScope::Workspace;
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
        reference_query_at(doc, selection, current_unit, current_symbols_with_native);
    const ReferenceScope scope =
        reference_scope_at(doc, params, query, selection, current_unit,
                           current_symbols_without_native, current_symbols_with_native);
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
        const ProjectIndex* candidate_index = document_project_index(candidate, false);
        const ModuleAst* candidate_unit = visible_document_unit(candidate_index, candidate);
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
        const std::vector<ReferenceLocation> locations =
            references_in(*candidate_unit, candidate, candidate_query);
        if (locations.empty()) {
            continue;
        }
        if (!target_module_document && !target_selective_document && native_identity.has_value()) {
            std::vector<Symbol> candidate_symbols_with_native;
            if (dudu_module_identity(*native_identity)) {
                if (const ModuleAst* candidate_visible =
                        visible_document_unit(candidate_index, candidate)) {
                    candidate_symbols_with_native =
                        symbols_for_module(*candidate_visible, true);
                }
            } else {
                const ProjectIndex* candidate_tree = document_project_index(candidate, true);
                if (const ModuleAst* candidate_visible =
                        visible_document_unit(candidate_tree, candidate)) {
                    candidate_symbols_with_native =
                        symbols_for_module(*candidate_visible, true);
                }
            }
            if (!document_has_native_identity_for_query(candidate_symbols_with_native,
                                                        candidate_query, *native_identity)) {
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
    const std::string old_name = selection.symbol.value_or("");
    const std::string new_name =
        params == nullptr ? std::string{} : string_value(params->get("newName"));
    const std::vector<Symbol> current_symbols_without_native =
        current_unit == nullptr ? std::vector<Symbol>{} : symbols_for_module(*current_unit, false);
    const RenameScope scope = rename_scope_at(doc, params, old_name, selection, current_unit,
                                              current_symbols_without_native);
    if (!valid_identifier(new_name) || scope == RenameScope::None) {
        return "null";
    }
    std::ostringstream out;
    out << "{\"changes\":{";
    bool first_doc = true;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        if (scope == RenameScope::CurrentDocument && candidate.uri != doc.uri) {
            continue;
        }
        const ProjectIndex* candidate_index = document_project_index(candidate, false);
        const ModuleAst* candidate_unit = visible_document_unit(candidate_index, candidate);
        if (candidate_unit == nullptr) {
            continue;
        }
        const std::vector<Symbol> candidate_symbols_without_native =
            symbols_for_module(*candidate_unit, false);
        if (scope == RenameScope::Workspace && candidate.uri != doc.uri &&
            document_declares_renameable_symbol(candidate, old_name,
                                                candidate_symbols_without_native)) {
            continue;
        }
        const std::vector<ReferenceLocation> locations =
            references_in(*candidate_unit, candidate, old_name);
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
