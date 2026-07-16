#include "dudu/lsp/language_server_lint_cpp_escape.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_visit.hpp"
#include "dudu/lsp/language_server_lint_common.hpp"

#include <set>

namespace dudu {
namespace {

void lint_cpp_escape_expr(const Expr& expr, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out);

void add_cpp_escape_diagnostic(const SourceLocation& location, std::set<std::pair<int, int>>& seen,
                               std::vector<Diagnostic>& out) {
    if (!seen.insert({location.line, location.column}).second) {
        return;
    }
    out.push_back({.location = location,
                   .message = "native interop hazard: raw cpp escape hatch",
                   .source = "dudu/lint",
                   .severity = 2,
                   .code = "dudu.lint.cpp_escape",
                   .data_name = "",
                   .fix_range = std::nullopt,
                   .related_information = {}});
}

void lint_cpp_escape_stmt(const Stmt& stmt, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
    if (!lint_same_source_file(stmt.location.file, doc.path)) {
        return;
    }
    if (stmt.kind == StmtKind::CppEscape) {
        add_cpp_escape_diagnostic(stmt.location, seen, out);
    }
    visit_stmt_expressions(stmt,
                           [&](const Expr& expr) { lint_cpp_escape_expr(expr, doc, seen, out); });
    for (const Stmt& child : stmt.children) {
        lint_cpp_escape_stmt(child, doc, seen, out);
    }
}

void lint_cpp_escape_expr(const Expr& expr, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
    visit_expr_tree(expr, [&](const Expr& node) {
        if (expr_missing(node)) {
            return;
        }
        if (node.kind == ExprKind::CppEscape &&
            lint_same_source_file(node.location.file, doc.path)) {
            add_cpp_escape_diagnostic(node.location, seen, out);
        }
    });
}

void lint_cpp_escape_function(const FunctionDecl& fn, const Document& doc,
                              std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
    for (const Stmt& stmt : fn.statements) {
        lint_cpp_escape_stmt(stmt, doc, seen, out);
    }
}

void lint_cpp_escape_class(const ClassDecl& klass, const Document& doc,
                           std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
    for (const FieldDecl& field : klass.fields) {
        lint_cpp_escape_expr(field.value_expr, doc, seen, out);
    }
    for (const ConstDecl& constant : klass.constants) {
        lint_cpp_escape_expr(constant.value_expr, doc, seen, out);
    }
    for (const ConstDecl& field : klass.static_fields) {
        lint_cpp_escape_expr(field.value_expr, doc, seen, out);
    }
    for (const FunctionDecl& method : klass.methods) {
        lint_cpp_escape_function(method, doc, seen, out);
    }
}

void lint_cpp_escape_module_impl(const ModuleAst& module, const Document& doc,
                                 std::set<std::pair<int, int>>& seen,
                                 std::vector<Diagnostic>& out) {
    for (const ConstDecl& constant : module.constants) {
        lint_cpp_escape_expr(constant.value_expr, doc, seen, out);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        lint_cpp_escape_expr(assertion.expression_expr, doc, seen, out);
    }
    for (const FunctionDecl& fn : module.functions) {
        lint_cpp_escape_function(fn, doc, seen, out);
    }
    for (const ClassDecl& klass : module.classes) {
        lint_cpp_escape_class(klass, doc, seen, out);
    }
    for (const ModuleAst& unit : module.module_units) {
        lint_cpp_escape_module_impl(unit, doc, seen, out);
    }
}

} // namespace

void lint_cpp_escape_module(const ModuleAst& module, const Document& doc,
                            std::vector<Diagnostic>& out) {
    std::set<std::pair<int, int>> seen;
    lint_cpp_escape_module_impl(module, doc, seen, out);
}

} // namespace dudu
