#include "dudu/unsupported.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema.hpp"

#include <array>
#include <cctype>

namespace dudu {
namespace {

struct UnsupportedPrefix {
    std::string_view prefix;
    std::string_view feature;
};

bool starts_statement(std::string_view text, std::string_view prefix) {
    if (text == prefix) {
        return true;
    }
    if (text.size() <= prefix.size() || text.substr(0, prefix.size()) != prefix) {
        return false;
    }
    const char next = text[prefix.size()];
    return next == ' ' || next == ':';
}

bool contains_call(std::string_view text, std::string_view name) {
    size_t pos = text.find(name);
    while (pos != std::string_view::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(text[pos - 1])) == 0 && text[pos - 1] != '_');
        const size_t open = pos + name.size();
        if (left_ok && open < text.size() && text[open] == '(') {
            return true;
        }
        pos = text.find(name, pos + name.size());
    }
    return false;
}

bool contains_word(std::string_view text, std::string_view name) {
    size_t pos = text.find(name);
    while (pos != std::string_view::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(text[pos - 1])) == 0 && text[pos - 1] != '_');
        const size_t end = pos + name.size();
        const bool right_ok =
            end == text.size() ||
            (std::isalnum(static_cast<unsigned char>(text[end])) == 0 && text[end] != '_');
        if (left_ok && right_ok) {
            return true;
        }
        pos = text.find(name, pos + name.size());
    }
    return false;
}

void check_unsupported_text(const SourceLocation& location, const std::string& text) {
    if (contains_call(text, "eval") || contains_call(text, "exec")) {
        throw CompileError(location, "unsupported Python feature: dynamic execution");
    }
    if (contains_call(text, "getattr") || contains_call(text, "setattr")) {
        throw CompileError(location, "unsupported Python feature: dynamic attribute access");
    }
    if (contains_word(text, "await")) {
        throw CompileError(location, "unsupported Python feature: async");
    }
}

void check_expr(const Expr& expr) {
    if (expr.text.empty()) {
        return;
    }
    if (expr.kind == ExprKind::Unknown) {
        check_unsupported_text(expr.location, expr.text);
        return;
    }
    if (expr.kind == ExprKind::Await) {
        throw CompileError(expr.location, "unsupported Python feature: async");
    }
    if (expr.kind == ExprKind::Lambda) {
        throw CompileError(expr.location,
                           "unsupported Python feature: lambda; declare a named function and "
                           "pass the function name");
    }
    if (expr.kind == ExprKind::Conditional) {
        throw CompileError(expr.location,
                           "unsupported Python feature: conditional expressions; use an "
                           "explicit if statement");
    }
    if (expr.kind == ExprKind::Yield) {
        throw CompileError(expr.location, "unsupported Python feature: generators");
    }
    if (expr.kind == ExprKind::Call) {
        const std::string callee = call_callee_text(expr);
        if (callee == "eval" || callee == "exec") {
            throw CompileError(expr.location, "unsupported Python feature: dynamic execution");
        }
        if (callee == "getattr" || callee == "setattr") {
            throw CompileError(expr.location,
                               "unsupported Python feature: dynamic attribute access");
        }
    }
    for (const Expr& child : expr.callee) {
        check_expr(child);
    }
    for (const Expr& child : expr.params) {
        check_expr(child);
    }
    for (const Expr& child : expr.template_args) {
        check_expr(child);
    }
    for (const Expr& child : expr.children) {
        check_expr(child);
    }
}

void check_statement_prefix(const Stmt& stmt) {
    const std::string text = trim_copy(stmt.text);
    constexpr std::array prefixes = {
        UnsupportedPrefix{"finally", "exceptions"},
        UnsupportedPrefix{"yield", "generators"},
        UnsupportedPrefix{"async", "async"},
        UnsupportedPrefix{"await", "async"},
        UnsupportedPrefix{"with", "context managers"},
        UnsupportedPrefix{"global", "global rebinding"},
        UnsupportedPrefix{"nonlocal", "nonlocal rebinding"},
        UnsupportedPrefix{"del", "dynamic deletion"},
        UnsupportedPrefix{"def", "local function declarations"},
        UnsupportedPrefix{"import", "local imports"},
        UnsupportedPrefix{"from", "local imports"},
    };
    for (const UnsupportedPrefix& prefix : prefixes) {
        if (starts_statement(text, prefix.prefix)) {
            throw CompileError(stmt.location,
                               "unsupported Python feature: " + std::string(prefix.feature));
        }
    }
}

void check_statement(const Stmt& stmt) {
    check_statement_prefix(stmt);
    if (stmt.kind == StmtKind::Unknown) {
        if (!trim_copy(stmt.text).empty()) {
            throw CompileError(stmt.location, "unsupported statement: " + trim_copy(stmt.text));
        }
        check_unsupported_text(stmt.location, trim_copy(stmt.text));
    } else {
        check_expr(stmt.expr);
        check_expr(stmt.value_expr);
        check_expr(stmt.target_expr);
        check_expr(stmt.condition_expr);
        check_expr(stmt.message_expr);
        check_expr(stmt.iterable_expr);
        check_expr(stmt.pattern_expr);
        check_expr(stmt.guard_expr);
    }
    for (const Stmt& child : stmt.children) {
        check_statement(child);
    }
}

} // namespace

void check_unsupported_python(const ModuleAst& module) {
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            for (const Stmt& stmt : method.statements) {
                check_statement(stmt);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        for (const Stmt& stmt : fn.statements) {
            check_statement(stmt);
        }
    }
}

} // namespace dudu
