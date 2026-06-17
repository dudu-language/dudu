#include "dudu/language_server_ast_lints.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_support.hpp"

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
    lint_suspicious_cast_expr(stmt.expr, doc, active_decls, out);
    lint_suspicious_cast_expr(stmt.value_expr, doc, active_decls, out);
    lint_suspicious_cast_expr(stmt.target_expr, doc, active_decls, out);
    lint_suspicious_cast_expr(stmt.condition_expr, doc, active_decls, out);
    lint_suspicious_cast_expr(stmt.message_expr, doc, active_decls, out);
    lint_suspicious_cast_expr(stmt.iterable_expr, doc, active_decls, out);
    lint_suspicious_cast_expr(stmt.pattern_expr, doc, active_decls, out);
    lint_suspicious_cast_expr(stmt.guard_expr, doc, active_decls, out);
    if (stmt.kind == StmtKind::VarDecl && !stmt.name.empty() && !stmt.type.empty()) {
        active_decls.push_back({.name = stmt.name,
                                .type = stmt.type,
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
    if (expr.kind == ExprKind::Unknown) {
        return;
    }
    if (expr.kind == ExprKind::Call && numeric_type_name(expr.name) && expr.children.size() == 1 &&
        expr.children.front().kind == ExprKind::Name &&
        same_source_file(expr.location.file, doc.path)) {
        const std::string& source_name = expr.children.front().name;
        const std::string source_type = visible_local_type(active_decls, source_name);
        if (!source_type.empty() && is_suspicious_numeric_cast(expr.name, source_type)) {
            out.push_back({.location = expr.location,
                           .message = "suspicious narrowing cast: " + expr.name + "(" +
                                      source_name + ") from " + source_type,
                           .source = "dudu/lint",
                           .severity = 2});
        }
    }
    for (const Expr& child : expr.callee) {
        lint_suspicious_cast_expr(child, doc, active_decls, out);
    }
    for (const Expr& child : expr.params) {
        lint_suspicious_cast_expr(child, doc, active_decls, out);
    }
    for (const Expr& child : expr.template_args) {
        lint_suspicious_cast_expr(child, doc, active_decls, out);
    }
    for (const Expr& child : expr.children) {
        lint_suspicious_cast_expr(child, doc, active_decls, out);
    }
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
                                .type = param.type,
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
                       .severity = 2});
    }
    lint_cpp_escape_expr(stmt.expr, doc, seen, out);
    lint_cpp_escape_expr(stmt.value_expr, doc, seen, out);
    lint_cpp_escape_expr(stmt.target_expr, doc, seen, out);
    lint_cpp_escape_expr(stmt.condition_expr, doc, seen, out);
    lint_cpp_escape_expr(stmt.message_expr, doc, seen, out);
    lint_cpp_escape_expr(stmt.iterable_expr, doc, seen, out);
    lint_cpp_escape_expr(stmt.pattern_expr, doc, seen, out);
    lint_cpp_escape_expr(stmt.guard_expr, doc, seen, out);
    for (const Stmt& child : stmt.children) {
        lint_cpp_escape_stmt(child, doc, seen, out);
    }
}

void lint_cpp_escape_expr(const Expr& expr, const Document& doc,
                          std::set<std::pair<int, int>>& seen, std::vector<Diagnostic>& out) {
    if (expr.kind == ExprKind::Unknown) {
        return;
    }
    if (expr.kind == ExprKind::CppEscape && same_source_file(expr.location.file, doc.path) &&
        seen.insert({expr.location.line, expr.location.column}).second) {
        out.push_back({.location = expr.location,
                       .message = "native interop hazard: raw cpp escape hatch",
                       .source = "dudu/lint",
                       .severity = 2});
    }
    for (const Expr& child : expr.callee) {
        lint_cpp_escape_expr(child, doc, seen, out);
    }
    for (const Expr& child : expr.params) {
        lint_cpp_escape_expr(child, doc, seen, out);
    }
    for (const Expr& child : expr.template_args) {
        lint_cpp_escape_expr(child, doc, seen, out);
    }
    for (const Expr& child : expr.children) {
        lint_cpp_escape_expr(child, doc, seen, out);
    }
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

} // namespace

std::vector<Diagnostic> ast_lint_diagnostics(const ModuleAst& module, const Document& doc) {
    std::vector<Diagnostic> out;
    std::set<std::pair<int, int>> seen_cpp_escapes;
    lint_cpp_escape_module(module, doc, seen_cpp_escapes, out);
    lint_suspicious_cast_module(module, doc, out);
    return out;
}

} // namespace dudu
