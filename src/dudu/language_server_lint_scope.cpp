#include "dudu/language_server_lint_scope.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/language_server_lint_common.hpp"

#include <map>

namespace dudu {
namespace {

struct AstLocalDecl {
    std::string name;
    SourceLocation location;
};

void collect_name_uses_stmt(const Stmt& stmt, const Document& doc,
                            std::map<std::string, std::vector<SourceLocation>>& uses) {
    visit_stmt_tree_expressions(stmt, [&](const Expr& expr) {
        if (expr.kind == ExprKind::Name && lint_same_source_file(expr.location.file, doc.path)) {
            uses[expr.name].push_back(expr.location);
        }
    });
}

bool active_decl_contains(const std::vector<AstLocalDecl>& active_decls, const std::string& name) {
    for (const AstLocalDecl& decl : active_decls) {
        if (decl.name == name) {
            return true;
        }
    }
    return false;
}

void add_scope_local(const std::string& name, const SourceLocation& location,
                     std::vector<AstLocalDecl>& active_decls, std::vector<AstLocalDecl>& locals,
                     std::vector<Diagnostic>& out, bool warn_shadow) {
    if (name.empty()) {
        return;
    }
    if (warn_shadow) {
        for (const AstLocalDecl& outer : active_decls) {
            if (outer.name == name) {
                out.push_back({.location = location,
                               .message = "local shadows outer binding: " + name,
                               .source = "dudu/lint",
                               .severity = 2,
                               .code = "dudu.lint.shadow",
                               .data_name = "",
                               .fix_range = std::nullopt});
                break;
            }
        }
    }
    AstLocalDecl local{.name = name, .location = location};
    locals.push_back(local);
    active_decls.push_back(std::move(local));
}

void collect_scope_lints_stmt_sequence(const std::vector<Stmt>& statements, const Document& doc,
                                       std::vector<AstLocalDecl> active_decls,
                                       std::vector<AstLocalDecl>& locals,
                                       std::vector<Diagnostic>& out) {
    for (const Stmt& stmt : statements) {
        if (stmt.kind == StmtKind::VarDecl && !stmt.name.empty() &&
            lint_same_source_file(stmt.location.file, doc.path)) {
            add_scope_local(stmt.name, stmt.location, active_decls, locals, out, true);
        } else if (stmt.kind == StmtKind::Assign &&
                   lint_same_source_file(stmt.location.file, doc.path)) {
            if (const std::vector<std::string> names = tuple_binding_names(stmt_target_expr(stmt));
                !names.empty()) {
                for (const std::string& name : names) {
                    if (!active_decl_contains(active_decls, name)) {
                        add_scope_local(name, stmt.location, active_decls, locals, out, false);
                    }
                }
            } else if (stmt_target_expr(stmt).kind == ExprKind::Name &&
                       !active_decl_contains(active_decls, stmt_target_expr(stmt).name)) {
                add_scope_local(stmt_target_expr(stmt).name, stmt_target_expr(stmt).location,
                                active_decls, locals, out, false);
            }
        } else if ((stmt.kind == StmtKind::For || stmt.kind == StmtKind::Except) &&
                   !stmt.name.empty() && lint_same_source_file(stmt.location.file, doc.path)) {
            std::vector<AstLocalDecl> nested_decls = active_decls;
            if (!active_decl_contains(nested_decls, stmt.name)) {
                add_scope_local(stmt.name, stmt.location, nested_decls, locals, out, false);
            }
            if (!stmt.children.empty()) {
                collect_scope_lints_stmt_sequence(stmt.children, doc, nested_decls, locals, out);
            }
            continue;
        }
        if (!stmt.children.empty()) {
            collect_scope_lints_stmt_sequence(stmt.children, doc, active_decls, locals, out);
        }
    }
}

bool location_after(const SourceLocation& use, const SourceLocation& decl) {
    if (use.line != decl.line) {
        return use.line > decl.line;
    }
    return use.column > decl.column;
}

void lint_scope_function(const FunctionDecl& fn, const Document& doc,
                         std::vector<Diagnostic>& out) {
    std::vector<AstLocalDecl> active_decls;
    for (const ParamDecl& param : fn.params) {
        active_decls.push_back({.name = param.name, .location = param.location});
    }
    std::vector<AstLocalDecl> locals;
    collect_scope_lints_stmt_sequence(fn.statements, doc, active_decls, locals, out);

    std::map<std::string, std::vector<SourceLocation>> uses;
    for (const Stmt& stmt : fn.statements) {
        collect_name_uses_stmt(stmt, doc, uses);
    }
    for (const AstLocalDecl& local : locals) {
        bool used = false;
        for (const SourceLocation& use : uses[local.name]) {
            if (location_after(use, local.location)) {
                used = true;
                break;
            }
        }
        if (!used) {
            out.push_back({.location = local.location,
                           .message = "unused local: " + local.name,
                           .source = "dudu/lint",
                           .severity = 2,
                           .code = "dudu.lint.unused",
                           .data_name = "",
                           .fix_range = lint_delete_line_range(local.location, doc)});
        }
    }
}

void lint_scope_class(const ClassDecl& klass, const Document& doc, std::vector<Diagnostic>& out) {
    for (const FunctionDecl& method : klass.methods) {
        lint_scope_function(method, doc, out);
    }
}

} // namespace

void lint_scope_module(const ModuleAst& module, const Document& doc, std::vector<Diagnostic>& out) {
    for (const FunctionDecl& fn : module.functions) {
        lint_scope_function(fn, doc, out);
    }
    for (const ClassDecl& klass : module.classes) {
        lint_scope_class(klass, doc, out);
    }
    for (const ModuleAst& unit : module.module_units) {
        lint_scope_module(unit, doc, out);
    }
}

} // namespace dudu
