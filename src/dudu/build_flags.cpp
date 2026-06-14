#include "dudu/build_flags.hpp"

#include "dudu/sema.hpp"

#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace dudu {
namespace {

bool is_ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

std::string strip_parens(std::string expr) {
    expr = trim(std::move(expr));
    while (expr.size() >= 2 && expr.front() == '(' && expr.back() == ')') {
        expr = trim(expr.substr(1, expr.size() - 2));
    }
    return expr;
}

std::optional<int64_t> eval_int(std::string expr, const std::map<std::string, int64_t>& constants) {
    expr = strip_parens(std::move(expr));
    if (expr.empty()) {
        return std::nullopt;
    }
    for (const char op : {'+', '-', '*', '/'}) {
        const size_t pos = expr.rfind(op);
        if (pos != std::string::npos && pos > 0) {
            const std::optional<int64_t> left = eval_int(expr.substr(0, pos), constants);
            const std::optional<int64_t> right = eval_int(expr.substr(pos + 1), constants);
            if (!left.has_value() || !right.has_value()) {
                return std::nullopt;
            }
            if (op == '+') {
                return *left + *right;
            }
            if (op == '-') {
                return *left - *right;
            }
            if (op == '*') {
                return *left * *right;
            }
            return *right == 0 ? std::nullopt : std::optional<int64_t>{*left / *right};
        }
    }
    if (std::isdigit(static_cast<unsigned char>(expr.front())) != 0) {
        return std::stoll(expr);
    }
    if (const auto it = constants.find(expr); it != constants.end()) {
        return it->second;
    }
    return std::nullopt;
}

void check_static_assert(const StaticAssertDecl& assertion,
                         const std::map<std::string, int64_t>& constants) {
    const std::string expr = strip_parens(assertion.expression);
    for (const std::string op : {"==", "!=", ">=", "<=", ">", "<"}) {
        const size_t pos = expr.find(op);
        if (pos == std::string::npos) {
            continue;
        }
        const std::optional<int64_t> left = eval_int(expr.substr(0, pos), constants);
        const std::optional<int64_t> right = eval_int(expr.substr(pos + op.size()), constants);
        if (!left.has_value() || !right.has_value()) {
            return;
        }
        bool passed = false;
        if (op == "==") {
            passed = *left == *right;
        } else if (op == "!=") {
            passed = *left != *right;
        } else if (op == ">=") {
            passed = *left >= *right;
        } else if (op == "<=") {
            passed = *left <= *right;
        } else if (op == ">") {
            passed = *left > *right;
        } else {
            passed = *left < *right;
        }
        if (!passed) {
            throw CompileError(assertion.location, "static_assert failed: " + assertion.expression);
        }
        return;
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

void check_body(const std::set<std::string>& names, const std::vector<Stmt>& body) {
    for (const Stmt& stmt : body) {
        check_text(names, stmt.location, stmt.text);
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
        check_text(names, constant.location, constant.value);
        if (const std::optional<int64_t> value = eval_int(constant.value, int_constants)) {
            int_constants[constant.name] = *value;
        }
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        check_text(names, assertion.location, assertion.expression);
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
