#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/language_server_ast_walk.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/parser.hpp"

namespace dudu {
namespace {

bool contains_name(const SourceLocation& location, const std::string& name,
                   const LspPosition& position) {
    if (name.empty() || location.line != position.line + 1 || location.column <= 0) {
        return false;
    }
    const int start = location.column;
    const int end = start + static_cast<int>(name.size());
    const int target_column = position.character + 1;
    return target_column >= start && target_column <= end;
}

void collect_call_callee_selection(const Expr& expr, const LspPosition& position,
                                   AstSelection& selection) {
    if (expr.kind != ExprKind::Call && expr.kind != ExprKind::TemplateCall) {
        return;
    }
    const std::optional<ExprPath> path = call_callee_path(expr);
    if (!path.has_value()) {
        return;
    }
    const std::string rendered_path = render_expr_path(*path);
    for (const ExprPathSegment& segment : path->segments) {
        if (!contains_name(segment.location, segment.text, position)) {
            continue;
        }
        selection.call_callee = true;
        if (!selection.symbol) {
            selection.symbol = segment.text;
        }
        if (!selection.symbol_path) {
            selection.symbol_path = rendered_path;
        }
        if (!selection.expr_path) {
            selection.expr_path = path;
        }
        return;
    }
}

void collect_selection_from_statements(const std::vector<Stmt>& statements,
                                       const LspPosition& position, AstSelection& selection) {
    const auto set_symbol = [&](const std::string& name, const SourceLocation& location) {
        if (!selection.symbol && contains_name(location, name, position)) {
            selection.symbol = name;
        }
    };
    const auto set_symbol_path = [&](const std::string& name, const SourceLocation& location) {
        if (!selection.symbol_path && contains_name(location, name, position)) {
            selection.symbol_path = name;
        }
    };
    const auto visit_type = [&](const TypeRef& type) {
        const std::string text = type_ref_text(type);
        set_symbol(text, type.location);
        set_symbol_path(text, type.location);
    };
    const auto visit_type_tree = [&](const TypeRef& type) {
        visit_type_ref_tree(type, visit_type);
    };
    const auto visit_expr = [&](const Expr& expr) {
        collect_call_callee_selection(expr, position, selection);
        if (!selection.call_callee &&
            (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
            expr.callee.size() == 1 && expr.callee.front().kind == ExprKind::Name &&
            contains_name(expr.callee.front().location, expr.callee.front().name, position)) {
            selection.call_callee = true;
        }
        if (expr.kind != ExprKind::Name && expr.kind != ExprKind::Member) {
            return;
        }
        const SourceLocation name_location = expr_name_location(expr);
        set_symbol(expr.name, name_location);
        std::string path_name = expr.name;
        if (expr.kind == ExprKind::Member) {
            if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
                path_name = render_expr_path(*path);
            }
        }
        set_symbol_path(path_name, name_location);
        if (!selection.expr_path && contains_name(name_location, expr.name, position)) {
            selection.expr_path = expr_path_from_expr(expr);
        }
    };
    visit_lsp_stmt_tree(statements, [&](const Stmt& stmt) {
        visit_stmt_binding_names(stmt, set_symbol);
        visit_stmt_binding_names(stmt, set_symbol_path);
        visit_type_tree(stmt.type_ref);
        visit_stmt_expressions(
            stmt, [&](const Expr& expr) { visit_lsp_expr_tree(expr, visit_expr, visit_type); });
    });
}

void collect_selection_from_module(const ModuleAst& module, const LspPosition& position,
                                   AstSelection& selection) {
    const auto set_symbol = [&](const std::string& name, const SourceLocation& location) {
        if (!selection.symbol && contains_name(location, name, position)) {
            selection.symbol = name;
        }
    };
    const auto set_symbol_path = [&](const std::string& name, const SourceLocation& location) {
        if (!selection.symbol_path && contains_name(location, name, position)) {
            selection.symbol_path = name;
        }
    };
    const auto visit_type = [&](const TypeRef& type) {
        const std::string text = type_ref_text(type);
        set_symbol(text, type.location);
        set_symbol_path(text, type.location);
    };
    const auto visit_type_tree = [&](const TypeRef& type) {
        visit_type_ref_tree(type, visit_type);
    };
    const auto visit_expr = [&](const Expr& expr) {
        collect_call_callee_selection(expr, position, selection);
        if (!selection.call_callee &&
            (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
            expr.callee.size() == 1 && expr.callee.front().kind == ExprKind::Name &&
            contains_name(expr.callee.front().location, expr.callee.front().name, position)) {
            selection.call_callee = true;
        }
        if (expr.kind != ExprKind::Name && expr.kind != ExprKind::Member) {
            return;
        }
        const SourceLocation name_location = expr_name_location(expr);
        set_symbol(expr.name, name_location);
        std::string path_name = expr.name;
        if (expr.kind == ExprKind::Member) {
            if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
                path_name = render_expr_path(*path);
            }
        }
        set_symbol_path(path_name, name_location);
        if (!selection.expr_path && contains_name(name_location, expr.name, position)) {
            selection.expr_path = expr_path_from_expr(expr);
        }
    };
    for (const ImportDecl& import : module.imports) {
        set_symbol(bound_import_name(import), import.location);
        set_symbol_path(bound_import_name(import), import.location);
        set_symbol(import.imported_name, import.location);
        set_symbol_path(import.imported_name, import.location);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        set_symbol(alias.name, alias.location);
        set_symbol_path(alias.name, alias.location);
        visit_type_tree(alias.type_ref);
    }
    for (const ConstDecl& constant : module.constants) {
        set_symbol(constant.name, constant.location);
        set_symbol_path(constant.name, constant.location);
        visit_type_tree(constant.type_ref);
        visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
    }
    for (const EnumDecl& en : module.enums) {
        set_symbol(en.name, en.location);
        set_symbol_path(en.name, en.location);
        visit_type_tree(en.underlying_type_ref);
        for (const EnumValueDecl& value : en.values) {
            set_symbol(value.name, value.location);
            set_symbol_path(value.name, value.location);
            for (const EnumPayloadField& field : value.payload_fields) {
                set_symbol(field.name, field.location);
                set_symbol_path(field.name, field.location);
                visit_type_tree(field.type_ref);
            }
            visit_lsp_expr_tree(value.value_expr, visit_expr, visit_type);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        set_symbol(klass.name, klass.location);
        set_symbol_path(klass.name, klass.location);
        for (const BaseClassDecl& base : klass.base_class_refs) {
            visit_type_tree(base.type_ref);
        }
        for (const FieldDecl& field : klass.fields) {
            set_symbol(field.name, field.location);
            set_symbol_path(field.name, field.location);
            visit_type_tree(field.type_ref);
            visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
        }
        for (const ConstDecl& constant : klass.constants) {
            set_symbol(constant.name, constant.location);
            set_symbol_path(constant.name, constant.location);
            visit_type_tree(constant.type_ref);
            visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
        }
        for (const ConstDecl& field : klass.static_fields) {
            set_symbol(field.name, field.location);
            set_symbol_path(field.name, field.location);
            visit_type_tree(field.type_ref);
            visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
        }
        for (const FunctionDecl& method : klass.methods) {
            set_symbol(method.name, method.location);
            set_symbol_path(method.name, method.location);
            visit_type_tree(method.receiver_type_ref);
            visit_type_tree(method.return_type_ref);
            for (const ParamDecl& param : method.params) {
                set_symbol(param.name, param.location);
                set_symbol_path(param.name, param.location);
                visit_type_tree(param.type_ref);
            }
            collect_selection_from_statements(method.statements, position, selection);
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        set_symbol(fn.name, fn.location);
        set_symbol_path(fn.name, fn.location);
        visit_type_tree(fn.receiver_type_ref);
        visit_type_tree(fn.return_type_ref);
        for (const ParamDecl& param : fn.params) {
            set_symbol(param.name, param.location);
            set_symbol_path(param.name, param.location);
            visit_type_tree(param.type_ref);
        }
        collect_selection_from_statements(fn.statements, position, selection);
    }
}

} // namespace

AstSelection ast_selection_at(const ModuleAst& module, const Json* params) {
    AstSelection selection;
    collect_selection_from_module(module, lsp_position(params), selection);
    return selection;
}

AstSelection ast_selection_at(const Document& doc, const Json* params) {
    try {
        return ast_selection_at(parse_source(doc.text, doc.path), params);
    } catch (const std::exception&) {
        return {};
    }
}

std::optional<std::string> ast_symbol_at(const Document& doc, const Json* params) {
    return ast_selection_at(doc, params).symbol;
}

std::optional<std::string> ast_symbol_path_at(const Document& doc, const Json* params) {
    return ast_selection_at(doc, params).symbol_path;
}

std::optional<ExprPath> ast_expr_path_at(const Document& doc, const Json* params) {
    return ast_selection_at(doc, params).expr_path;
}

} // namespace dudu
