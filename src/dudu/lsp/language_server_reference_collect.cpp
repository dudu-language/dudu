#include "dudu/lsp/language_server_reference_collect.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_operator.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace dudu {
namespace {

std::optional<std::string> self_owner_at_location(const ModuleAst& module,
                                                  const SourceLocation& location);

struct ReferenceCollector {
    const ModuleAst& module;
    const Document& doc;
    const std::string& query;
    std::vector<ReferenceLocation> out;
    std::set<std::pair<int, int>> seen;

    std::string query_member_tail() const {
        const size_t dot = query.rfind('.');
        return dot == std::string::npos ? std::string{} : query.substr(dot + 1);
    }

    void add_matched(const std::string& name, const SourceLocation& location, int range_size = 0) {
        if (name.empty() || location.line <= 0 || location.column <= 0) {
            return;
        }
        const auto key = std::pair{location.line, location.column};
        if (!seen.insert(key).second) {
            return;
        }
        const int line = location.line - 1;
        const int start = location.column - 1;
        const int width = range_size > 0 ? range_size : static_cast<int>(name.size());
        SourceRange source_range;
        source_range.start = location;
        source_range.end = location;
        source_range.end.column += width;
        out.push_back({.uri = uri_for_location(location, doc),
                       .range = range_json(line, start, start + width),
                       .source_range = std::move(source_range)});
    }

    void add(const std::string& name, const SourceLocation& location) {
        if (name == query) {
            add_matched(name, location);
        }
    }

    void add_member_decl(const std::string& owner, const std::string& member,
                         const SourceLocation& location) {
        if (owner + "." + member == query) {
            add_matched(member, location);
        }
    }

    bool member_receiver_matches_query_type(const std::string& receiver,
                                            const SourceLocation& location) const {
        const size_t dot = query.rfind('.');
        if (dot == std::string::npos || dot + 1 == query.size()) {
            return false;
        }
        const std::string owner = query.substr(0, dot);
        std::set<std::string> candidate_types;
        if (receiver == "self") {
            if (const std::optional<std::string> self_owner =
                    self_owner_at_location(module, location)) {
                candidate_types.insert(*self_owner);
            }
        }
        const std::map<std::string, TypeRef> locals =
            local_type_refs_before_location(module, location);
        const auto found = locals.find(receiver);
        if (found != locals.end()) {
            const std::set<std::string> local_candidates =
                member_candidate_types(module, found->second);
            candidate_types.insert(local_candidates.begin(), local_candidates.end());
        }
        return candidate_types.contains(owner);
    }

    void visit_type(const TypeRef& type) {
        add(type_ref_text(type), type.location);
    }

    void visit_type_tree(const TypeRef& type) {
        visit_type_ref_tree(type, [&](const TypeRef& child) { visit_type(child); });
    }

    void visit_expr(const Expr& expr) {
        if (expr.kind == ExprKind::Name) {
            add(expr.name, expr_name_location(expr));
        } else if (expr.kind == ExprKind::Member) {
            if (const std::optional<ExprPath> path = expr_path_from_expr(expr);
                path.has_value() && render_expr_path(*path) == query) {
                add_matched(expr.name, expr_name_location(expr));
            } else if (path.has_value() && query.find('.') == std::string::npos &&
                       !path->segments.empty() &&
                       path->segments.front().kind == ExprPathSegmentKind::Name &&
                       path->segments.front().text == query) {
                add_matched(query, path->segments.front().location);
            } else if (path.has_value() && path->segments.size() == 2 &&
                       path->segments[0].kind == ExprPathSegmentKind::Name &&
                       path->segments[1].kind == ExprPathSegmentKind::Name &&
                       path->segments[1].text == query_member_tail() &&
                       member_receiver_matches_query_type(path->segments[0].text,
                                                          expr_name_location(expr))) {
                add_matched(expr.name, expr_name_location(expr));
            }
        } else if (expr.kind == ExprKind::Binary) {
            if (dudu_operator_query_exists(module, query)) {
                if (const std::optional<Symbol> op =
                        dudu_operator_symbol_for_expr(module, expr, expr.op_location.line);
                    op && op->name == query) {
                    add_matched(std::string(expr.op), expr.op_location);
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
                visit_lsp_expr_tree(
                    expr, [&](const Expr& child) { visit_expr(child); },
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

bool location_matches_file(const SourceLocation& location, const SourceLocation& target) {
    if (location.file.empty() || target.file.empty()) {
        return true;
    }
    const std::filesystem::path left(location.file.str());
    const std::filesystem::path right(target.file.str());
    return same_path(left, right) ||
           (!left.filename().empty() && left.filename() == right.filename());
}

bool function_contains_line(const FunctionDecl& function, int one_based_line) {
    return function.location.line <= one_based_line &&
           one_based_line <= std::max(function.location.line, function_end_line(function));
}

bool function_contains_location(const FunctionDecl& function, const SourceLocation& location) {
    return location_matches_file(function.location, location) &&
           function_contains_line(function, location.line);
}

std::optional<std::string> self_owner_at_location(const ModuleAst& module,
                                                  const SourceLocation& location) {
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (function_contains_location(method, location)) {
                return klass.name;
            }
        }
    }
    return std::nullopt;
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
                                                      const ModuleAst& module, const Document& doc,
                                                      const std::string& query) {
    ReferenceCollector collector{
        .module = module, .doc = doc, .query = query, .out = {}, .seen = {}};
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
    ReferenceCollector collector{
        .module = module, .doc = doc, .query = query, .out = {}, .seen = {}};
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
        visit_lsp_expr_tree(
            constant.value_expr, [&](const Expr& expr) { collector.visit_expr(expr); },
            [&](const TypeRef& type) { collector.visit_type(type); });
    }
    for (const EnumDecl& en : module.enums) {
        collector.add(en.name, en.location);
        collector.visit_type_tree(en.underlying_type_ref);
        for (const EnumValueDecl& value : en.values) {
            collector.add(value.name, value.location);
            collector.add_member_decl(en.name, value.name, value.location);
            for (const EnumPayloadField& field : value.payload_fields) {
                collector.add(field.name, field.location);
                collector.visit_type_tree(field.type_ref);
            }
            visit_lsp_expr_tree(
                value.value_expr, [&](const Expr& expr) { collector.visit_expr(expr); },
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
            collector.add_member_decl(klass.name, field.name, field.location);
            collector.visit_type_tree(field.type_ref);
            visit_lsp_expr_tree(
                field.value_expr, [&](const Expr& expr) { collector.visit_expr(expr); },
                [&](const TypeRef& type) { collector.visit_type(type); });
        }
        for (const ConstDecl& constant : klass.constants) {
            collector.add(constant.name, constant.location);
            collector.add_member_decl(klass.name, constant.name, constant.location);
            collector.visit_type_tree(constant.type_ref);
            visit_lsp_expr_tree(
                constant.value_expr, [&](const Expr& expr) { collector.visit_expr(expr); },
                [&](const TypeRef& type) { collector.visit_type(type); });
        }
        for (const ConstDecl& field : klass.static_fields) {
            collector.add(field.name, field.location);
            collector.add_member_decl(klass.name, field.name, field.location);
            collector.visit_type_tree(field.type_ref);
            visit_lsp_expr_tree(
                field.value_expr, [&](const Expr& expr) { collector.visit_expr(expr); },
                [&](const TypeRef& type) { collector.visit_type(type); });
        }
        for (const FunctionDecl& method : klass.methods) {
            collector.add(method.name, method.location);
            collector.add_member_decl(klass.name, method.name, method.location);
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

std::optional<std::vector<ReferenceLocation>> references_in_local_scope(const ModuleAst& module,
                                                                        const Document& doc,
                                                                        const std::string& query,
                                                                        int one_based_line) {
    if (query.empty() || query.find('.') != std::string::npos) {
        return std::nullopt;
    }
    const auto find_in_function =
        [&](const FunctionDecl& function) -> std::optional<std::vector<ReferenceLocation>> {
        if (!function_contains_line(function, one_based_line) ||
            !function_binds_name(function, query)) {
            return std::nullopt;
        }
        return references_in_function(function, module, doc, query);
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
