#include "dudu/lsp/language_server_local_definition.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

namespace dudu {
namespace {

bool location_before_cursor(const SourceLocation& location, const LspPosition& position) {
    const int line = position.line + 1;
    const int column = position.character + 1;
    return location.line < line || (location.line == line && location.column <= column);
}

void update_binding(const std::string& name, const SourceLocation& location,
                    const std::string& query, const LspPosition& position,
                    std::optional<SourceLocation>& result) {
    if (name == query && location_before_cursor(location, position)) {
        result = location;
    }
}

bool nested_binding_scope(StmtKind kind) {
    return kind == StmtKind::For || kind == StmtKind::Except || kind == StmtKind::Case;
}

void collect_visible_bindings(const std::vector<Stmt>& statements, const LspPosition& position,
                              const std::string& query,
                              std::optional<SourceLocation>& result) {
    const int cursor_line = position.line + 1;
    for (const Stmt& stmt : statements) {
        if (stmt.location.line > cursor_line) {
            break;
        }
        if (!nested_binding_scope(stmt.kind)) {
            visit_stmt_binding_names(stmt, [&](const std::string& name, SourceLocation location) {
                update_binding(name, location, query, position, result);
            });
        }
        if (stmt.children.empty() || !statement_contains_source_line(stmt, cursor_line)) {
            continue;
        }
        std::optional<SourceLocation> nested_result = result;
        if (nested_binding_scope(stmt.kind)) {
            visit_stmt_binding_names(stmt, [&](const std::string& name, SourceLocation location) {
                update_binding(name, location, query, position, nested_result);
            });
        }
        collect_visible_bindings(stmt.children, position, query, nested_result);
        result = nested_result;
        return;
    }
}

std::optional<SourceLocation> definition_in_function(const FunctionDecl& function,
                                                     const LspPosition& position,
                                                     const std::string& query) {
    if (!function_contains_source_line(function, position.line + 1)) {
        return std::nullopt;
    }
    std::optional<SourceLocation> result;
    for (const ParamDecl& param : function.params) {
        update_binding(param.name, param.location, query, position, result);
    }
    collect_visible_bindings(function.statements, position, query, result);
    return result;
}

} // namespace

std::optional<std::string> local_definition_json(const Document& doc, const ModuleAst& module,
                                                 const Json* params,
                                                 const std::string& query) {
    if (query.empty() || query.find('.') != std::string::npos) {
        return std::nullopt;
    }
    const LspPosition position = lsp_position(params);
    for (const FunctionDecl& function : module.functions) {
        if (const std::optional<SourceLocation> found =
                definition_in_function(function, position, query)) {
            return location_json(uri_for_location(*found, doc), range_json(*found));
        }
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (const std::optional<SourceLocation> found =
                    definition_in_function(method, position, query)) {
                return location_json(uri_for_location(*found, doc), range_json(*found));
            }
        }
    }
    return std::nullopt;
}

} // namespace dudu
