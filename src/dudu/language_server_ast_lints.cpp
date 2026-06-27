#include "dudu/language_server_ast_lints.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_lint_common.hpp"
#include "dudu/language_server_lint_scope.hpp"
#include "dudu/language_server_lint_suspicious_cast.hpp"
#include "dudu/language_server_lint_unreachable.hpp"
#include "dudu/language_server_support.hpp"

#include <set>

namespace dudu {
namespace {

void lint_cpp_escape_expr(const Expr& expr, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out);

void lint_cpp_escape_stmt(const Stmt& stmt, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
    if (!lint_same_source_file(stmt.location.file, doc.path)) {
        return;
    }
    if (stmt.kind == StmtKind::CppEscape &&
        seen.insert({stmt.location.line, stmt.location.column}).second) {
        out.push_back({.location = stmt.location,
                       .message = "native interop hazard: raw cpp escape hatch",
                       .source = "dudu/lint",
                       .severity = 2,
                       .code = "dudu.lint.cpp_escape",
                       .data_name = ""});
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
            lint_same_source_file(node.location.file, doc.path) &&
            seen.insert({node.location.line, node.location.column}).second) {
            out.push_back({.location = node.location,
                           .message = "native interop hazard: raw cpp escape hatch",
                           .source = "dudu/lint",
                           .severity = 2,
                           .code = "dudu.lint.cpp_escape",
                           .data_name = ""});
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

void lint_cpp_escape_module(const ModuleAst& module, const Document& doc,
                            std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
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
        lint_cpp_escape_module(unit, doc, seen, out);
    }
}

} // namespace

std::vector<Diagnostic> ast_lint_diagnostics(const ModuleAst& module, const Document& doc) {
    std::vector<Diagnostic> out;
    std::set<std::pair<int, int>> seen_cpp_escapes;
    lint_cpp_escape_module(module, doc, seen_cpp_escapes, out);
    lint_suspicious_cast_module(module, doc, out);
    lint_unreachable_module(module, doc, out);
    lint_scope_module(module, doc, out);
    return out;
}

} // namespace dudu
