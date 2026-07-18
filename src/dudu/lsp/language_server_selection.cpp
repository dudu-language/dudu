#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/ast_visit.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

#include <algorithm>
#include <cstddef>

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

bool contains_operator(const Expr& expr, const LspPosition& position) {
    if (expr.kind != ExprKind::Binary || expr.op_location.line <= 0) {
        return false;
    }
    return contains_name(expr.op_location, std::string(expr.op), position);
}

bool contains_index_operator(const Expr& expr, const LspPosition& position) {
    if (expr.kind != ExprKind::Index || expr.children.size() != 2) {
        return false;
    }
    const Expr& receiver = expr.children[0];
    const Expr& index = expr.children[1];
    if (receiver.range.end.line != position.line + 1 || index.location.line != position.line + 1) {
        return false;
    }
    const int target_column = position.character + 1;
    return target_column >= receiver.range.end.column && target_column < index.location.column;
}

ExprPath expr_path_prefix(const ExprPath& path, std::size_t last_segment_index) {
    ExprPath prefix;
    if (path.segments.empty()) {
        return prefix;
    }
    const std::size_t end = std::min(last_segment_index + 1, path.segments.size());
    prefix.segments.assign(path.segments.begin(), path.segments.begin() + end);
    return prefix;
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
    for (std::size_t index = 0; index < path->segments.size(); ++index) {
        const ExprPathSegment& segment = path->segments[index];
        if (!contains_name(segment.location, segment.text, position)) {
            continue;
        }
        const ExprPath selected_path = expr_path_prefix(*path, index);
        selection.call_callee = index + 1 == path->segments.size();
        if (selection.call_callee) {
            selection.call_expr = expr;
        }
        selection.symbol = segment.text;
        selection.symbol_path = render_expr_path(selected_path);
        selection.expr_path = selected_path;
        return;
    }
}

bool collect_expr_path_selection(const Expr& expr, const LspPosition& position,
                                 AstSelection& selection) {
    const std::optional<ExprPath> path = expr_path_from_expr(expr);
    if (!path.has_value()) {
        return false;
    }
    for (std::size_t index = 0; index < path->segments.size(); ++index) {
        const ExprPathSegment& segment = path->segments[index];
        if (!contains_name(segment.location, segment.text, position)) {
            continue;
        }
        const ExprPath selected_path = expr_path_prefix(*path, index);
        selection.symbol = segment.text;
        selection.symbol_path = render_expr_path(selected_path);
        selection.expr_path = selected_path;
        return true;
    }
    return false;
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
        const std::string head = type_ref_head_name(type);
        set_symbol(head, type.location);
        set_symbol_path(head, type.location);
        const std::string text = type_ref_text(type);
        set_symbol(text, type.location);
        set_symbol_path(text, type.location);
    };
    const auto visit_type_tree = [&](const TypeRef& type, const auto& visit_type_tree_ref) -> void {
        if (!has_type_ref(type)) {
            return;
        }
        for (const TypeRef& child : type.children) {
            visit_type_tree_ref(child, visit_type_tree_ref);
        }
        visit_type(type);
    };
    const auto visit_expr = [&](const Expr& expr) {
        collect_call_callee_selection(expr, position, selection);
        if (!selection.operator_expr && contains_operator(expr, position)) {
            selection.symbol = std::string(expr.op);
            selection.symbol_path = std::string(expr.op);
            selection.operator_expr = expr;
            return;
        }
        if (!selection.operator_expr && contains_index_operator(expr, position)) {
            selection.symbol = "[]";
            selection.symbol_path = "[]";
            selection.operator_expr = expr;
            return;
        }
        if (!selection.call_callee &&
            (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
            expr_callee(expr).size() == 1 && expr_callee(expr).front().kind == ExprKind::Name &&
            contains_name(expr_callee(expr).front().location, expr_callee(expr).front().name,
                          position)) {
            selection.call_callee = true;
        }
        if (expr.kind != ExprKind::Name && expr.kind != ExprKind::Member) {
            return;
        }
        if (expr.kind == ExprKind::Member &&
            collect_expr_path_selection(expr, position, selection)) {
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
        visit_type_tree(stmt_type_ref(stmt), visit_type_tree);
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
        const std::string head = type_ref_head_name(type);
        set_symbol(head, type.location);
        set_symbol_path(head, type.location);
        const std::string text = type_ref_text(type);
        set_symbol(text, type.location);
        set_symbol_path(text, type.location);
    };
    const auto visit_type_tree = [&](const TypeRef& type, const auto& visit_type_tree_ref) -> void {
        if (!has_type_ref(type)) {
            return;
        }
        for (const TypeRef& child : type.children) {
            visit_type_tree_ref(child, visit_type_tree_ref);
        }
        visit_type(type);
    };
    const auto visit_expr = [&](const Expr& expr) {
        collect_call_callee_selection(expr, position, selection);
        if (!selection.operator_expr && contains_operator(expr, position)) {
            selection.symbol = std::string(expr.op);
            selection.symbol_path = std::string(expr.op);
            selection.operator_expr = expr;
            return;
        }
        if (!selection.operator_expr && contains_index_operator(expr, position)) {
            selection.symbol = "[]";
            selection.symbol_path = "[]";
            selection.operator_expr = expr;
            return;
        }
        if (!selection.call_callee &&
            (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
            expr_callee(expr).size() == 1 && expr_callee(expr).front().kind == ExprKind::Name &&
            contains_name(expr_callee(expr).front().location, expr_callee(expr).front().name,
                          position)) {
            selection.call_callee = true;
        }
        if (expr.kind != ExprKind::Name && expr.kind != ExprKind::Member) {
            return;
        }
        if (expr.kind == ExprKind::Member &&
            collect_expr_path_selection(expr, position, selection)) {
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
        set_symbol(import.module_path, import.module_range.start);
        set_symbol_path(import.module_path, import.module_range.start);
        set_symbol(import.imported_name, import.imported_name_range.start);
        set_symbol_path(import.imported_name, import.imported_name_range.start);
        if (!import.alias.empty()) {
            set_symbol(import.alias, import.alias_range.start);
            set_symbol_path(import.alias, import.alias_range.start);
        }
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        set_symbol(alias.name, alias.location);
        set_symbol_path(alias.name, alias.location);
        visit_type_tree(alias.type_ref, visit_type_tree);
    }
    for (const ConstDecl& constant : module.constants) {
        set_symbol(constant.name, constant.location);
        set_symbol_path(constant.name, constant.location);
        visit_type_tree(constant.type_ref, visit_type_tree);
        visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
    }
    for (const EnumDecl& en : module.enums) {
        set_symbol(en.name, en.location);
        set_symbol_path(en.name, en.location);
        visit_type_tree(en.underlying_type_ref, visit_type_tree);
        for (const EnumValueDecl& value : en.values) {
            set_symbol(value.name, value.location);
            set_symbol_path(value.name, value.location);
            for (const EnumPayloadField& field : value.payload_fields) {
                set_symbol(field.name, field.location);
                set_symbol_path(field.name, field.location);
                visit_type_tree(field.type_ref, visit_type_tree);
            }
            visit_lsp_expr_tree(value.value_expr, visit_expr, visit_type);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        set_symbol(klass.name, klass.location);
        set_symbol_path(klass.name, klass.location);
        for (const GenericParamDecl& param : klass.generic_param_decls) {
            set_symbol(param.name, param.location);
            set_symbol_path(param.name, param.location);
        }
        for (const BaseClassDecl& base : klass.base_class_refs) {
            visit_type_tree(base.type_ref, visit_type_tree);
        }
        for (const FieldDecl& field : klass.fields) {
            set_symbol(field.name, field.location);
            set_symbol_path(field.name, field.location);
            visit_type_tree(field.type_ref, visit_type_tree);
            visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
        }
        for (const ConstDecl& constant : klass.constants) {
            set_symbol(constant.name, constant.location);
            set_symbol_path(constant.name, constant.location);
            visit_type_tree(constant.type_ref, visit_type_tree);
            visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
        }
        for (const ConstDecl& field : klass.static_fields) {
            set_symbol(field.name, field.location);
            set_symbol_path(field.name, field.location);
            visit_type_tree(field.type_ref, visit_type_tree);
            visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
        }
        for (const FunctionDecl& method : klass.methods) {
            set_symbol(method.name, method.location);
            set_symbol_path(method.name, method.location);
            for (const GenericParamDecl& param : method.generic_param_decls) {
                set_symbol(param.name, param.location);
                set_symbol_path(param.name, param.location);
            }
            visit_type_tree(method.receiver_type_ref, visit_type_tree);
            visit_type_tree(method.return_type_ref, visit_type_tree);
            for (const ParamDecl& param : method.params) {
                set_symbol(param.name, param.location);
                set_symbol_path(param.name, param.location);
                visit_type_tree(param.type_ref, visit_type_tree);
            }
            collect_selection_from_statements(method.statements, position, selection);
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        set_symbol(fn.name, fn.location);
        set_symbol_path(fn.name, fn.location);
        for (const GenericParamDecl& param : fn.generic_param_decls) {
            set_symbol(param.name, param.location);
            set_symbol_path(param.name, param.location);
        }
        visit_type_tree(fn.receiver_type_ref, visit_type_tree);
        visit_type_tree(fn.return_type_ref, visit_type_tree);
        for (const ParamDecl& param : fn.params) {
            set_symbol(param.name, param.location);
            set_symbol_path(param.name, param.location);
            visit_type_tree(param.type_ref, visit_type_tree);
        }
        collect_selection_from_statements(fn.statements, position, selection);
    }
}

} // namespace

AstSelection ast_selection_at(const ModuleAst& module, const Json* params) {
    return ast_selection_at(module, lsp_position(params));
}

AstSelection ast_selection_at(const ModuleAst& module, LspPosition position) {
    AstSelection selection;
    collect_selection_from_module(module, position, selection);
    return selection;
}

} // namespace dudu
