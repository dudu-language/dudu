#include "dudu/language_server_references.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_local_context.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_native_lookup.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/module_names.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
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
                                              const std::string& name) {
    for (const Symbol& symbol : symbols_for_document(doc, false)) {
        if (symbol.name == name && renameable_symbol_kind(symbol.kind) &&
            std::filesystem::path(symbol.location.file) == doc.path &&
            position_contains_name(params, name, symbol.location)) {
            return symbol;
        }
    }
    return std::nullopt;
}

bool document_declares_renameable_symbol(const Document& doc, const std::string& name) {
    for (const Symbol& symbol : symbols_for_document(doc, false)) {
        if (symbol.name == name && renameable_symbol_kind(symbol.kind) &&
            std::filesystem::path(symbol.location.file) == doc.path) {
            return true;
        }
    }
    return false;
}

std::optional<Symbol> unique_document_declaration_for_references(const Document& doc,
                                                                 const std::string& name) {
    if (name.empty() || name.find('.') != std::string::npos) {
        return std::nullopt;
    }
    std::set<std::string> reference_ranges;
    for (const ReferenceLocation& location : references_in(doc, name)) {
        reference_ranges.insert(symbol_range_key(location.uri, location.range));
    }
    std::optional<Symbol> declaration;
    for (const Symbol& symbol : symbols_for_document(doc, false)) {
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

bool selected_call_callee(const Document& doc, const Json* params, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    bool matched = false;
    const auto visit_expr = [&](const Expr& expr) {
        if (matched || (expr.kind != ExprKind::Call && expr.kind != ExprKind::TemplateCall) ||
            expr.callee.size() != 1) {
            return;
        }
        const Expr& callee = expr.callee.front();
        if (callee.kind != ExprKind::Name || callee.name != name) {
            return;
        }
        if (position_contains_name(params, name, callee.location)) {
            matched = true;
        }
    };
    const std::function<void(const std::vector<Stmt>&)> visit_stmts =
        [&](const std::vector<Stmt>& statements) {
            for (const Stmt& stmt : statements) {
                visit_stmt_tree_expressions(stmt, visit_expr);
            }
        };
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ClassDecl& klass : module.classes) {
            for (const FunctionDecl& method : klass.methods) {
                visit_stmts(method.statements);
            }
        }
        for (const FunctionDecl& fn : module.functions) {
            visit_stmts(fn.statements);
        }
    } catch (const std::exception&) {
        return false;
    }
    return matched;
}

bool document_has_type_symbol(const Document& doc, const std::string& name) {
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol.name == name &&
            (symbol.kind == lsp_symbol_kind::Class || symbol.kind == lsp_symbol_kind::Struct)) {
            return true;
        }
    }
    return false;
}

bool document_has_direct_native_symbol(const Document& doc, const std::string& name) {
    if (name.empty() || name.find('.') != std::string::npos) {
        return false;
    }
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol.name != name || std::filesystem::path(symbol.location.file) == doc.path) {
            continue;
        }
        if (symbol.kind == lsp_symbol_kind::Class || symbol.kind == lsp_symbol_kind::Struct ||
            symbol.kind == lsp_symbol_kind::Enum || symbol.kind == lsp_symbol_kind::Function ||
            symbol.kind == lsp_symbol_kind::Constant ||
            symbol.kind == lsp_symbol_kind::Namespace) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> dotted_source_symbol_at(const Document& doc, const Json* params) {
    const LspPosition position = lsp_position(params);
    int line = 0;
    size_t line_start = 0;
    while (line < position.line) {
        const size_t newline = doc.text.find('\n', line_start);
        if (newline == std::string::npos) {
            return std::nullopt;
        }
        line_start = newline + 1;
        ++line;
    }
    const size_t line_end = doc.text.find('\n', line_start);
    const std::string_view text = std::string_view(doc.text).substr(
        line_start, line_end == std::string::npos ? std::string::npos : line_end - line_start);
    if (position.character < 0 || static_cast<size_t>(position.character) > text.size()) {
        return std::nullopt;
    }
    size_t start = static_cast<size_t>(position.character);
    while (start > 0 && symbol_char(text[start - 1])) {
        --start;
    }
    size_t end = static_cast<size_t>(position.character);
    while (end < text.size() && symbol_char(text[end])) {
        ++end;
    }
    if (start >= end) {
        return std::nullopt;
    }
    return std::string(text.substr(start, end - start));
}

std::string dotted_head(const std::string& query) {
    const size_t dot = query.find('.');
    return dot == std::string::npos ? query : query.substr(0, dot);
}

std::string dotted_tail(const std::string& query) {
    const size_t dot = query.rfind('.');
    return dot == std::string::npos ? query : query.substr(dot + 1);
}

struct ImportReferenceTarget {
    std::string source_key;
    std::string member_name;
};

bool same_import_reference_target(const std::optional<ImportReferenceTarget>& lhs,
                                  const std::optional<ImportReferenceTarget>& rhs) {
    if (!lhs.has_value() || !rhs.has_value()) {
        return lhs.has_value() == rhs.has_value();
    }
    return lhs->source_key == rhs->source_key && lhs->member_name == rhs->member_name;
}

std::optional<ImportReferenceTarget> selective_import_target(const Document& doc,
                                                             const std::string& query) {
    if (query.empty() || query.find('.') != std::string::npos) {
        return std::nullopt;
    }
    try {
        const ModuleAst module = module_for_document(doc, false);
        const ModuleAst& current = visible_module_unit(module, doc.path);
        for (const ImportDecl& import : current.imports) {
            if (import.kind != ImportKind::From || bound_import_name(import) != query) {
                continue;
            }
            if (const ModuleAst* imported = imported_module_unit(module, current, import)) {
                return ImportReferenceTarget{
                    .source_key = imported->source_path.empty() ? imported->module_path
                                                               : imported->source_path.string(),
                    .member_name = import.imported_name};
            }
            std::error_code error;
            const std::filesystem::path base =
                current.source_path.empty() ? doc.path : current.source_path;
            const std::filesystem::path resolved = std::filesystem::weakly_canonical(
                module_path_to_file(base.parent_path(), import.module_path), error);
            if (!error) {
                return ImportReferenceTarget{.source_key = resolved.string(),
                                             .member_name = import.imported_name};
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> module_import_target_key(const Document& doc,
                                                    const std::string& dotted_query) {
    if (dotted_query.find('.') == std::string::npos) {
        return std::nullopt;
    }
    const std::string head = dotted_head(dotted_query);
    try {
        const ModuleAst module = module_for_document(doc, false);
        const ModuleAst& current = visible_module_unit(module, doc.path);
        for (const ImportDecl& import : current.imports) {
            if (import.kind != ImportKind::Module || bound_import_name(import) != head) {
                continue;
            }
            if (const ModuleAst* imported = imported_module_unit(module, current, import)) {
                return imported->source_path.empty() ? imported->module_path
                                                     : imported->source_path.string();
            }
            std::error_code error;
            const std::filesystem::path base =
                current.source_path.empty() ? doc.path : current.source_path;
            const std::filesystem::path resolved = std::filesystem::weakly_canonical(
                module_path_to_file(base.parent_path(), import.module_path), error);
            if (!error) {
                return resolved.string();
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::string reference_query_at(const Document& doc, const Json* params) {
    const std::string name = ast_symbol_at(doc, params).value_or("");
    std::optional<std::string> expression_path;
    std::vector<std::string> paths;
    if (const std::optional<std::string> path = ast_symbol_path_at(doc, params)) {
        paths.push_back(*path);
    }
    if (const std::optional<ExprPath> path = ast_expr_path_at(doc, params)) {
        expression_path = render_expr_path(*path);
        paths.push_back(*expression_path);
    }
    if (const std::optional<std::string> path = dotted_source_symbol_at(doc, params)) {
        paths.push_back(*path);
    }
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const std::string& path : paths) {
            if (path.empty() || path == name || path.find('.') == std::string::npos) {
                continue;
            }
            if (native_alias_target_class_definition(module, path).has_value()) {
                return path;
            }
            if (module_import_target_key(doc, path).has_value()) {
                return path;
            }
            for (const ClassDecl& klass : module.native_classes) {
                if (klass.name == path) {
                    return path;
                }
            }
            for (const NativeFunctionDecl& fn : module.native_functions) {
                if (fn.name == path) {
                    return path;
                }
            }
            for (const NativeValueDecl& value : module.native_values) {
                if (value.name == path) {
                    return path;
                }
            }
            for (const NativeMacroDecl& macro : module.native_macros) {
                if (macro.name == path) {
                    return path;
                }
            }
            if (document_has_type_symbol(doc, path)) {
                return path;
            }
        }
    } catch (const std::exception&) {
        return name;
    }
    if (expression_path.has_value() && expression_path->find('.') != std::string::npos) {
        return *expression_path;
    }
    return name;
}

RenameScope rename_scope_at(const Document& doc, const Json* params, const std::string& name) {
    if (declaration_at_position(doc, params, name).has_value()) {
        return RenameScope::Workspace;
    }
    const std::optional<Symbol> declaration = unique_document_declaration_for_references(doc, name);
    if (!declaration.has_value()) {
        return RenameScope::None;
    }
    if (has_type_ref(local_type_ref_before_cursor(doc, name, params))) {
        return RenameScope::None;
    }
    if (ast_symbol_at(doc, params).value_or("") == name &&
        selected_call_callee(doc, params, name)) {
        return RenameScope::CurrentDocument;
    }
    return RenameScope::None;
}

ReferenceScope reference_scope_at(const Document& doc, const Json* params,
                                  const std::string& name) {
    if (name.empty()) {
        return ReferenceScope::None;
    }
    if (declaration_at_position(doc, params, name).has_value()) {
        return ReferenceScope::WorkspaceSkipRedeclarations;
    }
    if (const std::optional<std::string> path = ast_symbol_path_at(doc, params);
        path.has_value() && path->find('.') != std::string::npos) {
        return ReferenceScope::Workspace;
    }
    if (selective_import_target(doc, name).has_value()) {
        return ReferenceScope::Workspace;
    }
    if (unique_document_declaration_for_references(doc, name).has_value()) {
        return ReferenceScope::CurrentDocument;
    }
    if (document_has_direct_native_symbol(doc, name)) {
        return ReferenceScope::CurrentDocument;
    }
    return ReferenceScope::None;
}

} // namespace

std::string references_json(const Document& doc, const Json* params,
                            const std::map<std::string, Document>& workspace) {
    const std::string query = reference_query_at(doc, params);
    const ReferenceScope scope = reference_scope_at(doc, params, query);
    if (scope == ReferenceScope::None) {
        return "[]";
    }
    const std::optional<std::string> module_target =
        scope == ReferenceScope::Workspace ? module_import_target_key(doc, query) : std::nullopt;
    const std::optional<ImportReferenceTarget> selective_target =
        scope == ReferenceScope::Workspace ? selective_import_target(doc, query) : std::nullopt;
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        if (scope == ReferenceScope::CurrentDocument && candidate.uri != doc.uri) {
            continue;
        }
        if (scope == ReferenceScope::WorkspaceSkipRedeclarations && candidate.uri != doc.uri &&
            document_declares_renameable_symbol(candidate, query)) {
            continue;
        }
        std::error_code path_error;
        const std::filesystem::path candidate_path =
            candidate.path.empty()
                ? std::filesystem::path{}
                : std::filesystem::weakly_canonical(candidate.path, path_error);
        const bool target_module_document = module_target.has_value() && !candidate.path.empty() &&
                                            !path_error &&
                                            candidate_path.string() == *module_target;
        const bool target_selective_document = selective_target.has_value() &&
                                               !candidate.path.empty() && !path_error &&
                                               candidate_path.string() ==
                                                   selective_target->source_key;
        if (module_target.has_value() && candidate.uri != doc.uri && !target_module_document &&
            module_import_target_key(candidate, query) != module_target) {
            continue;
        }
        if (selective_target.has_value() && candidate.uri != doc.uri && !target_selective_document &&
            !same_import_reference_target(selective_import_target(candidate, query),
                                          selective_target)) {
            continue;
        }
        const std::string candidate_query =
            target_module_document      ? dotted_tail(query)
            : target_selective_document ? selective_target->member_name
                                        : query;
        for (const ReferenceLocation& location : references_in(candidate, candidate_query)) {
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
    const RenameScope scope = rename_scope_at(doc, params, old_name);
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
        if (scope == RenameScope::Workspace && candidate.uri != doc.uri &&
            document_declares_renameable_symbol(candidate, old_name)) {
            continue;
        }
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
