#include "dudu/build_flags.hpp"

#include "dudu/sema.hpp"
#include "dudu/sema_common.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace dudu {
namespace {

bool is_ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::optional<int64_t> parse_int_literal(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    if (text.empty()) {
        return std::nullopt;
    }
    size_t parsed = 0;
    const int64_t value = std::stoll(text, &parsed, 0);
    return parsed == text.size() ? std::optional<int64_t>{value} : std::nullopt;
}

std::optional<int64_t> eval_int(const Expr& expr, const std::map<std::string, int64_t>& constants) {
    switch (expr.kind) {
    case ExprKind::IntLiteral:
        return parse_int_literal(expr.text);
    case ExprKind::Name:
        if (const auto it = constants.find(expr.name); it != constants.end()) {
            return it->second;
        }
        return std::nullopt;
    case ExprKind::Unary:
        if (expr.children.size() != 1) {
            return std::nullopt;
        }
        if (expr.op == "-") {
            if (const std::optional<int64_t> value = eval_int(expr.children.front(), constants)) {
                return -*value;
            }
        }
        return std::nullopt;
    case ExprKind::Binary:
        if (expr.children.size() != 2) {
            return std::nullopt;
        }
        {
            const std::optional<int64_t> left = eval_int(expr.children[0], constants);
            const std::optional<int64_t> right = eval_int(expr.children[1], constants);
            if (!left.has_value() || !right.has_value()) {
                return std::nullopt;
            }
            if (expr.op == "+") {
                return *left + *right;
            }
            if (expr.op == "-") {
                return *left - *right;
            }
            if (expr.op == "*") {
                return *left * *right;
            }
            if (expr.op == "/" && *right != 0) {
                return *left / *right;
            }
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

void check_static_assert(const StaticAssertDecl& assertion,
                         const std::map<std::string, int64_t>& constants) {
    const Expr& expr = assertion.expression_expr;
    if (expr.kind != ExprKind::Binary || expr.children.size() != 2) {
        return;
    }
    const std::optional<int64_t> left = eval_int(expr.children[0], constants);
    const std::optional<int64_t> right = eval_int(expr.children[1], constants);
    if (!left.has_value() || !right.has_value()) {
        return;
    }
    bool passed = false;
    if (expr.op == "==") {
        passed = *left == *right;
    } else if (expr.op == "!=") {
        passed = *left != *right;
    } else if (expr.op == ">=") {
        passed = *left >= *right;
    } else if (expr.op == "<=") {
        passed = *left <= *right;
    } else if (expr.op == ">") {
        passed = *left > *right;
    } else if (expr.op == "<") {
        passed = *left < *right;
    } else {
        return;
    }
    if (!passed) {
        throw CompileError(assertion.location, "static_assert failed: " + assertion.expression);
    }
}

void check_text(const std::set<std::string>& names, const SourceLocation& location,
                const std::string& text) {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (text.compare(i, 6, "build.") != 0) {
            continue;
        }
        const size_t start = i + 6;
        size_t end = start;
        while (end < text.size() && is_ident(text[end])) {
            ++end;
        }
        const std::string name = text.substr(start, end - start);
        if (!names.contains(name)) {
            throw CompileError(location, "unknown build flag: build." + name);
        }
        i = end;
    }
}

void check_expr(const std::set<std::string>& names, const Expr& expr) {
    if (!sema_has_expr(expr)) {
        return;
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 &&
        expr.children.front().kind == ExprKind::Name && expr.children.front().name == "build") {
        if (!names.contains(expr.name)) {
            throw CompileError(expr.location, "unknown build flag: build." + expr.name);
        }
    }
    for (const Expr& child : expr.callee) {
        check_expr(names, child);
    }
    for (const Expr& child : expr.params) {
        check_expr(names, child);
    }
    for (const Expr& child : expr.template_args) {
        check_expr(names, child);
    }
    for (const Expr& child : expr.children) {
        check_expr(names, child);
    }
}

void check_stmt(const std::set<std::string>& names, const Stmt& stmt) {
    if (stmt.kind == StmtKind::CppEscape) {
        check_text(names, stmt.location, stmt.text);
        return;
    }
    check_expr(names, stmt.expr);
    check_expr(names, stmt.value_expr);
    check_expr(names, stmt.target_expr);
    check_expr(names, stmt.condition_expr);
    check_expr(names, stmt.message_expr);
    check_expr(names, stmt.iterable_expr);
}

void check_body(const std::set<std::string>& names, const std::vector<Stmt>& body) {
    for (const Stmt& stmt : body) {
        check_stmt(names, stmt);
        check_body(names, stmt.children);
    }
}

} // namespace

void check_build_flags(const ModuleAst& module) {
    std::set<std::string> names = {"DEBUG", "RENDER_BACKEND"};
    std::map<std::string, int64_t> int_constants;
    for (const auto& [name, _] : module.build_values) {
        names.insert(name);
    }
    for (const ConstDecl& constant : module.constants) {
        check_expr(names, constant.value_expr);
        if (const std::optional<int64_t> value = eval_int(constant.value_expr, int_constants)) {
            int_constants[constant.name] = *value;
        }
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        check_expr(names, assertion.expression_expr);
        check_static_assert(assertion, int_constants);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            check_body(names, method.statements);
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        check_body(names, fn.statements);
    }
}

} // namespace dudu
