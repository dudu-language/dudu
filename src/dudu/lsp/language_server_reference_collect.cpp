#include "dudu/lsp/language_server_reference_collect.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

namespace dudu {
namespace {

struct ReferenceCollector {
    const Document& doc;
    const std::string& query;
    std::vector<ReferenceLocation> out;
    std::set<std::pair<int, int>> seen;

    void add_matched(const std::string& name, const SourceLocation& location) {
        if (name.empty() || location.line <= 0 || location.column <= 0) {
            return;
        }
        const auto key = std::pair{location.line, location.column};
        if (!seen.insert(key).second) {
            return;
        }
        const int line = location.line - 1;
        const int start = location.column - 1;
        out.push_back({uri_for_location(location, doc),
                       range_json(line, start, start + static_cast<int>(name.size()))});
    }

    void add(const std::string& name, const SourceLocation& location) {
        if (name == query) {
            add_matched(name, location);
        }
    }

    void visit_type(const TypeRef& type) {
        add(type_ref_text(type), type.location);
    }

    void visit_type_tree(const TypeRef& type) {
        visit_type_ref_tree(type, [&](const TypeRef& child) { visit_type(child); });
    }

    void visit_expr(const Expr& expr) {
        if (expr.kind == ExprKind::Name || expr.kind == ExprKind::Member) {
            add(expr.name, expr_name_location(expr));
            if (expr.kind == ExprKind::Member) {
                if (const std::optional<ExprPath> path = expr_path_from_expr(expr);
                    path.has_value() && render_expr_path(*path) == query) {
                    add_matched(expr.name, expr_name_location(expr));
                }
            }
        }
    }

    void visit_statements(const std::vector<Stmt>& statements) {
        visit_lsp_stmt_tree(statements, [&](const Stmt& stmt) {
            visit_stmt_binding_names(stmt, [&](const std::string& name, SourceLocation location) {
                add(name, location);
            });
            visit_type_tree(stmt_type_ref(stmt));
            visit_stmt_expressions(stmt, [&](const Expr& expr) {
                visit_lsp_expr_tree(expr, [&](const Expr& child) { visit_expr(child); },
                                    [&](const TypeRef& type) { visit_type(type); });
            });
        });
    }
};

int function_end_line(const FunctionDecl& function) {
    int line = function.location.line;
    for (const Stmt& stmt : function.statements) {
        line = std::max(line, stmt.location.line);
        line = std::max(line, stmt.range.end.line);
    }
    return line;
}

bool function_contains_line(const FunctionDecl& function, int one_based_line) {
    return function.location.line <= one_based_line &&
           one_based_line <= std::max(function.location.line, function_end_line(function));
}

bool function_binds_name(const FunctionDecl& function, const std::string& query) {
    for (const ParamDecl& param : function.params) {
        if (param.name == query) {
            return true;
        }
    }
    bool found = false;
    visit_lsp_stmt_tree(function.statements, [&](const Stmt& stmt) {
        visit_stmt_binding_names(stmt, [&](const std::string& name, SourceLocation) {
            if (name == query) {
                found = true;
            }
        });
    });
    return found;
}

std::vector<ReferenceLocation> references_in_function(const FunctionDecl& function,
                                                      const Document& doc,
                                                      const std::string& query) {
    ReferenceCollector collector{.doc = doc, .query = query, .out = {}, .seen = {}};
    for (const ParamDecl& param : function.params) {
        collector.add(param.name, param.location);
        collector.visit_type_tree(param.type_ref);
    }
    collector.visit_type_tree(function.receiver_type_ref);
    collector.visit_type_tree(function.return_type_ref);
    collector.visit_statements(function.statements);
    return std::move(collector.out);
}

} // namespace

std::vector<ReferenceLocation> references_in(const ModuleAst& module, const Document& doc,
                                             const std::string& query) {
    if (query.empty()) {
        return {};
    }
    ReferenceCollector collector{.doc = doc, .query = query, .out = {}, .seen = {}};
    const auto visit_stmts = [&](const std::vector<Stmt>& statements) {
        collector.visit_statements(statements);
    };
    for (const ImportDecl& import : module.imports) {
        collector.add(bound_import_name(import), import.location);
        collector.add(import.imported_name, import.location);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        collector.add(alias.name, alias.location);
        collector.visit_type_tree(alias.type_ref);
    }
    for (const ConstDecl& constant : module.constants) {
        collector.add(constant.name, constant.location);
        collector.visit_type_tree(constant.type_ref);
        visit_lsp_expr_tree(constant.value_expr, [&](const Expr& expr) { collector.visit_expr(expr); },
                            [&](const TypeRef& type) { collector.visit_type(type); });
    }
    for (const EnumDecl& en : module.enums) {
        collector.add(en.name, en.location);
        collector.visit_type_tree(en.underlying_type_ref);
        for (const EnumValueDecl& value : en.values) {
            collector.add(value.name, value.location);
            for (const EnumPayloadField& field : value.payload_fields) {
                collector.add(field.name, field.location);
                collector.visit_type_tree(field.type_ref);
            }
            visit_lsp_expr_tree(value.value_expr,
                                [&](const Expr& expr) { collector.visit_expr(expr); },
                                [&](const TypeRef& type) { collector.visit_type(type); });
        }
    }
    for (const ClassDecl& klass : module.classes) {
        collector.add(klass.name, klass.location);
        for (const BaseClassDecl& base : klass.base_class_refs) {
            collector.visit_type_tree(base.type_ref);
        }
        for (const FieldDecl& field : klass.fields) {
            collector.add(field.name, field.location);
            collector.visit_type_tree(field.type_ref);
            visit_lsp_expr_tree(field.value_expr,
                                [&](const Expr& expr) { collector.visit_expr(expr); },
                                [&](const TypeRef& type) { collector.visit_type(type); });
        }
        for (const ConstDecl& constant : klass.constants) {
            collector.add(constant.name, constant.location);
            collector.visit_type_tree(constant.type_ref);
            visit_lsp_expr_tree(constant.value_expr,
                                [&](const Expr& expr) { collector.visit_expr(expr); },
                                [&](const TypeRef& type) { collector.visit_type(type); });
        }
        for (const ConstDecl& field : klass.static_fields) {
            collector.add(field.name, field.location);
            collector.visit_type_tree(field.type_ref);
            visit_lsp_expr_tree(field.value_expr,
                                [&](const Expr& expr) { collector.visit_expr(expr); },
                                [&](const TypeRef& type) { collector.visit_type(type); });
        }
        for (const FunctionDecl& method : klass.methods) {
            collector.add(method.name, method.location);
            collector.visit_type_tree(method.receiver_type_ref);
            collector.visit_type_tree(method.return_type_ref);
            for (const ParamDecl& param : method.params) {
                collector.add(param.name, param.location);
                collector.visit_type_tree(param.type_ref);
            }
            visit_stmts(method.statements);
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        collector.add(fn.name, fn.location);
        collector.visit_type_tree(fn.receiver_type_ref);
        collector.visit_type_tree(fn.return_type_ref);
        for (const ParamDecl& param : fn.params) {
            collector.add(param.name, param.location);
            collector.visit_type_tree(param.type_ref);
        }
        visit_stmts(fn.statements);
    }
    return std::move(collector.out);
}

std::optional<std::vector<ReferenceLocation>>
references_in_local_scope(const ModuleAst& module, const Document& doc, const std::string& query,
                          int one_based_line) {
    if (query.empty() || query.find('.') != std::string::npos) {
        return std::nullopt;
    }
    const auto find_in_function = [&](const FunctionDecl& function)
        -> std::optional<std::vector<ReferenceLocation>> {
        if (!function_contains_line(function, one_based_line) ||
            !function_binds_name(function, query)) {
            return std::nullopt;
        }
        return references_in_function(function, doc, query);
    };
    for (const FunctionDecl& function : module.functions) {
        if (const auto found = find_in_function(function)) {
            return found;
        }
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (const auto found = find_in_function(method)) {
                return found;
            }
        }
    }
    return std::nullopt;
}

} // namespace dudu
