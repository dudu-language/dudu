#include "dudu/language_server_references.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_local_context.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
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

bool renameable_symbol_kind(const int kind) {
    return kind == 5 || kind == 6 || kind == 8 || kind == 10 || kind == 12 || kind == 14;
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
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol.name == name && renameable_symbol_kind(symbol.kind) &&
            std::filesystem::path(symbol.location.file) == doc.path &&
            position_contains_name(params, name, symbol.location)) {
            return symbol;
        }
    }
    return std::nullopt;
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
    for (const Symbol& symbol : symbols_for_document(doc)) {
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
