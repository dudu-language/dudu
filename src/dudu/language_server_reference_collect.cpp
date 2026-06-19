#include "dudu/language_server_navigation.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/language_server_ast_walk.hpp"
#include "dudu/parser.hpp"

#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace dudu {

std::vector<ReferenceLocation> references_in(const Document& doc, const std::string& query) {
    std::vector<ReferenceLocation> out;
    if (query.empty()) {
        return out;
    }
    std::set<std::pair<int, int>> seen;
    const auto add_matched = [&](const std::string& name, const SourceLocation& location) {
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
    };
    const auto add = [&](const std::string& name, const SourceLocation& location) {
        if (name != query) {
            return;
        }
        add_matched(name, location);
    };
    const std::function<void(const TypeRef&)> visit_type = [&](const TypeRef& type) {
        if (!has_type_ref(type)) {
            return;
        }
        add(type_ref_text(type), type.location);
        for (const TypeRef& child : type.children) {
            visit_type(child);
        }
    };
    const std::function<void(const Expr&)> visit_expr = [&](const Expr& expr) {
        if (expr.kind == ExprKind::Name || expr.kind == ExprKind::Member) {
            add(expr.name, expr_name_location(expr));
            if (expr.kind == ExprKind::Member) {
                if (const std::optional<ExprPath> path = expr_path_from_expr(expr);
                    path.has_value() && render_expr_path(*path) == query) {
                    add_matched(expr.name, expr_name_location(expr));
                }
            }
        }
        for (const Expr& callee : expr.callee) {
            visit_expr(callee);
        }
        for (const Expr& param : expr.params) {
            visit_expr(param);
        }
        for (const Expr& arg : expr.template_args) {
            visit_expr(arg);
        }
        for (const TypeRef& arg : expr.template_type_args) {
            visit_type(arg);
        }
        visit_type(expr.type_ref);
        for (const Expr& child : expr.children) {
            visit_expr(child);
        }
    };
    const std::function<void(const std::vector<Stmt>&)> visit_stmts =
        [&](const std::vector<Stmt>& statements) {
            for (const Stmt& stmt : statements) {
                visit_stmt_binding_names(stmt, add);
                visit_type(stmt.type_ref);
                visit_stmt_expressions(stmt, visit_expr);
                visit_stmts(stmt.children);
            }
        };
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ImportDecl& import : module.imports) {
            add(bound_import_name(import), import.location);
            add(import.imported_name, import.location);
        }
        for (const TypeAliasDecl& alias : module.aliases) {
            add(alias.name, alias.location);
            visit_type(alias.type_ref);
        }
        for (const ConstDecl& constant : module.constants) {
            add(constant.name, constant.location);
            visit_type(constant.type_ref);
            visit_expr(constant.value_expr);
        }
        for (const EnumDecl& en : module.enums) {
            add(en.name, en.location);
            visit_type(en.underlying_type_ref);
            for (const EnumValueDecl& value : en.values) {
                add(value.name, value.location);
                for (const EnumPayloadField& field : value.payload_fields) {
                    add(field.name, field.location);
                    visit_type(field.type_ref);
                }
                visit_expr(value.value_expr);
            }
        }
        for (const ClassDecl& klass : module.classes) {
            add(klass.name, klass.location);
            for (const BaseClassDecl& base : klass.base_class_refs) {
                visit_type(base.type_ref);
            }
            for (const FieldDecl& field : klass.fields) {
                add(field.name, field.location);
                visit_type(field.type_ref);
                visit_expr(field.value_expr);
            }
            for (const ConstDecl& constant : klass.constants) {
                add(constant.name, constant.location);
                visit_type(constant.type_ref);
                visit_expr(constant.value_expr);
            }
            for (const ConstDecl& field : klass.static_fields) {
                add(field.name, field.location);
                visit_type(field.type_ref);
                visit_expr(field.value_expr);
            }
            for (const FunctionDecl& method : klass.methods) {
                add(method.name, method.location);
                visit_type(method.receiver_type_ref);
                visit_type(method.return_type_ref);
                for (const ParamDecl& param : method.params) {
                    add(param.name, param.location);
                    visit_type(param.type_ref);
                }
                visit_stmts(method.statements);
            }
        }
        for (const FunctionDecl& fn : module.functions) {
            add(fn.name, fn.location);
            visit_type(fn.receiver_type_ref);
            visit_type(fn.return_type_ref);
            for (const ParamDecl& param : fn.params) {
                add(param.name, param.location);
                visit_type(param.type_ref);
            }
            visit_stmts(fn.statements);
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

} // namespace dudu
