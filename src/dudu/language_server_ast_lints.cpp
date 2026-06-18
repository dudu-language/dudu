#include "dudu/language_server_ast_lints.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/sema_common.hpp"

#include <filesystem>
#include <map>
#include <set>

namespace dudu {
namespace {

struct AstLintLocal {
    std::string name;
    std::string type;
    int line = 0;
    int column = 0;
};

struct AstLocalDecl {
    std::string name;
    SourceLocation location;
};

bool numeric_type_name(const std::string& name) {
    static const std::set<std::string> types = {"i8",  "i16", "i32",   "i64",   "u8",  "u16",
                                                "u32", "u64", "isize", "usize", "f32", "f64"};
    return types.contains(name);
}

bool is_suspicious_numeric_cast(const std::string& target, std::string source) {
    source = trim_copy(std::move(source));
    if (target == source) {
        return false;
    }
    static const std::map<std::string, int> integer_bits = {
        {"i8", 8},   {"u8", 8},   {"i16", 16}, {"u16", 16},   {"i32", 32},
        {"u32", 32}, {"i64", 64}, {"u64", 64}, {"isize", 64}, {"usize", 64},
    };
    const bool source_float = source == "f32" || source == "f64";
    const auto source_integer = integer_bits.find(source);
    const auto target_integer = integer_bits.find(target);
    if (source_float && target_integer != integer_bits.end()) {
        return true;
    }
    if (source == "f64" && target == "f32") {
        return true;
    }
    if (source_integer != integer_bits.end() && target_integer != integer_bits.end() &&
        target_integer->second < source_integer->second) {
        return true;
    }
    if (source_integer != integer_bits.end() && target == "f32" && source_integer->second > 24) {
        return true;
    }
    return false;
}

bool same_source_file(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return lhs == rhs;
    }
    std::error_code error;
    const std::filesystem::path lhs_canonical = std::filesystem::weakly_canonical(lhs, error);
    if (error) {
        return lhs.lexically_normal() == rhs.lexically_normal();
    }
    const std::filesystem::path rhs_canonical = std::filesystem::weakly_canonical(rhs, error);
    if (error) {
        return lhs.lexically_normal() == rhs.lexically_normal();
    }
    return lhs_canonical == rhs_canonical;
}

std::string visible_local_type(const std::vector<AstLintLocal>& active_decls,
                               const std::string& name) {
    for (auto it = active_decls.rbegin(); it != active_decls.rend(); ++it) {
        if (it->name == name) {
            return it->type;
        }
    }
    return {};
}

void lint_suspicious_cast_expr(const Expr& expr, const Document& doc,
                               const std::vector<AstLintLocal>& active_decls,
                               std::vector<Diagnostic>& out);

void lint_suspicious_cast_statement_sequence(const std::vector<Stmt>& statements,
                                             const Document& doc,
                                             std::vector<AstLintLocal> active_decls,
                                             std::vector<Diagnostic>& out);

void lint_suspicious_cast_stmt(const Stmt& stmt, const Document& doc,
                               std::vector<AstLintLocal>& active_decls,
                               std::vector<Diagnostic>& out) {
    if (!same_source_file(stmt.location.file, doc.path)) {
        return;
    }
    visit_stmt_expressions(stmt, [&](const Expr& expr) {
        lint_suspicious_cast_expr(expr, doc, active_decls, out);
    });
    if (stmt.kind == StmtKind::VarDecl && !stmt.name.empty() && has_type_ref(stmt.type_ref)) {
        active_decls.push_back({.name = stmt.name,
                                .type = substitute_type_ref_text(stmt.type_ref, {}),
                                .line = stmt.location.line,
                                .column = stmt.location.column});
    }
    if (!stmt.children.empty()) {
        lint_suspicious_cast_statement_sequence(stmt.children, doc, active_decls, out);
    }
}

void lint_suspicious_cast_expr(const Expr& expr, const Document& doc,
                               const std::vector<AstLintLocal>& active_decls,
                               std::vector<Diagnostic>& out) {
    visit_expr_tree(expr, [&](const Expr& node) {
        if (expr_missing(node)) {
            return;
        }
        const std::string callee = direct_callee_name(node);
        if (node.kind == ExprKind::Call && numeric_type_name(callee) && node.children.size() == 1 &&
            node.children.front().kind == ExprKind::Name &&
            same_source_file(node.location.file, doc.path)) {
            const std::string& source_name = node.children.front().name;
            const std::string source_type = visible_local_type(active_decls, source_name);
            if (!source_type.empty() && is_suspicious_numeric_cast(callee, source_type)) {
                out.push_back({.location = node.location,
                               .message = "suspicious narrowing cast: " + callee + "(" +
                                          source_name + ") from " + source_type,
                               .source = "dudu/lint",
                               .severity = 2,
                               .code = "dudu.lint.suspicious_cast",
                               .data_name = ""});
            }
        }
    });
}

void lint_suspicious_cast_statement_sequence(const std::vector<Stmt>& statements,
                                             const Document& doc,
                                             std::vector<AstLintLocal> active_decls,
                                             std::vector<Diagnostic>& out) {
    for (const Stmt& stmt : statements) {
        lint_suspicious_cast_stmt(stmt, doc, active_decls, out);
    }
}

void lint_suspicious_cast_function(const FunctionDecl& fn, const Document& doc,
                                   std::vector<Diagnostic>& out) {
    std::vector<AstLintLocal> active_decls;
    for (const ParamDecl& param : fn.params) {
        active_decls.push_back({.name = param.name,
                                .type = substitute_type_ref_text(param.type_ref, {}),
                                .line = param.location.line,
                                .column = param.location.column});
    }
    lint_suspicious_cast_statement_sequence(fn.statements, doc, active_decls, out);
}

void lint_suspicious_cast_class(const ClassDecl& klass, const Document& doc,
                                std::vector<Diagnostic>& out) {
    for (const FieldDecl& field : klass.fields) {
        lint_suspicious_cast_expr(field.value_expr, doc, {}, out);
    }
    for (const ConstDecl& constant : klass.constants) {
        lint_suspicious_cast_expr(constant.value_expr, doc, {}, out);
    }
    for (const ConstDecl& field : klass.static_fields) {
        lint_suspicious_cast_expr(field.value_expr, doc, {}, out);
    }
    for (const FunctionDecl& method : klass.methods) {
        lint_suspicious_cast_function(method, doc, out);
    }
}

void lint_cpp_escape_expr(const Expr& expr, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out);

void lint_cpp_escape_stmt(const Stmt& stmt, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
    if (!same_source_file(stmt.location.file, doc.path)) {
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
        if (node.kind == ExprKind::CppEscape && same_source_file(node.location.file, doc.path) &&
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

void lint_suspicious_cast_module(const ModuleAst& module, const Document& doc,
                                 std::vector<Diagnostic>& out) {
    for (const ConstDecl& constant : module.constants) {
        lint_suspicious_cast_expr(constant.value_expr, doc, {}, out);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        lint_suspicious_cast_expr(assertion.expression_expr, doc, {}, out);
    }
    for (const FunctionDecl& fn : module.functions) {
        lint_suspicious_cast_function(fn, doc, out);
    }
    for (const ClassDecl& klass : module.classes) {
        lint_suspicious_cast_class(klass, doc, out);
    }
    for (const ModuleAst& unit : module.module_units) {
        lint_suspicious_cast_module(unit, doc, out);
    }
}

void lint_unreachable_statement_sequence(const std::vector<Stmt>& statements, const Document& doc,
                                         std::vector<Diagnostic>& out) {
    bool after_return = false;
    for (const Stmt& stmt : statements) {
        const bool in_document = same_source_file(stmt.location.file, doc.path);
        if (after_return && in_document) {
            out.push_back({.location = stmt.location,
                           .message = "unreachable statement after return",
                           .source = "dudu/lint",
                           .severity = 2,
                           .code = "dudu.lint.unreachable",
                           .data_name = ""});
        }
        if (!stmt.children.empty()) {
            lint_unreachable_statement_sequence(stmt.children, doc, out);
        }
        if (stmt.kind == StmtKind::Return && in_document) {
            after_return = true;
        }
    }
}

void lint_unreachable_function(const FunctionDecl& fn, const Document& doc,
                               std::vector<Diagnostic>& out) {
    lint_unreachable_statement_sequence(fn.statements, doc, out);
}

void lint_unreachable_class(const ClassDecl& klass, const Document& doc,
                            std::vector<Diagnostic>& out) {
    for (const FunctionDecl& method : klass.methods) {
        lint_unreachable_function(method, doc, out);
    }
}

void lint_unreachable_module(const ModuleAst& module, const Document& doc,
                             std::vector<Diagnostic>& out) {
    for (const FunctionDecl& fn : module.functions) {
        lint_unreachable_function(fn, doc, out);
    }
    for (const ClassDecl& klass : module.classes) {
        lint_unreachable_class(klass, doc, out);
    }
    for (const ModuleAst& unit : module.module_units) {
        lint_unreachable_module(unit, doc, out);
    }
}

void collect_name_uses_stmt(const Stmt& stmt, const Document& doc,
                            std::map<std::string, std::vector<SourceLocation>>& uses) {
    visit_stmt_tree_expressions(stmt, [&](const Expr& expr) {
        if (expr.kind == ExprKind::Name && same_source_file(expr.location.file, doc.path)) {
            uses[expr.name].push_back(expr.location);
        }
    });
}

void collect_scope_lints_stmt_sequence(const std::vector<Stmt>& statements, const Document& doc,
                                       std::vector<AstLocalDecl> active_decls,
                                       std::vector<AstLocalDecl>& locals,
                                       std::vector<Diagnostic>& out) {
    for (const Stmt& stmt : statements) {
        if (stmt.kind == StmtKind::VarDecl && !stmt.name.empty() &&
            same_source_file(stmt.location.file, doc.path)) {
            for (const AstLocalDecl& outer : active_decls) {
                if (outer.name == stmt.name) {
                    out.push_back({.location = stmt.location,
                                   .message = "local shadows outer binding: " + stmt.name,
                                   .source = "dudu/lint",
                                   .severity = 2,
                                   .code = "dudu.lint.shadow",
                                   .data_name = ""});
                    break;
                }
            }
            AstLocalDecl local{.name = stmt.name, .location = stmt.location};
            locals.push_back(local);
            active_decls.push_back(std::move(local));
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
                           .data_name = ""});
        }
    }
}

void lint_scope_class(const ClassDecl& klass, const Document& doc, std::vector<Diagnostic>& out) {
    for (const FunctionDecl& method : klass.methods) {
        lint_scope_function(method, doc, out);
    }
}

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
