#include "dudu/sema.hpp"

#include "dudu/build_flags.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/escapes.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/type_compat.hpp"
#include "dudu/unsupported.hpp"

#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

struct FunctionScope {
    const Symbols& symbols;
    std::map<std::string, std::string> locals;
    std::set<std::string> constants;
};

std::string member_path_type(const FunctionScope& scope, const RawStmt* stmt,
                             const std::string& path) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        if (const auto local = scope.locals.find(path); local != scope.locals.end()) {
            return local->second;
        }
        return {};
    }

    std::string current = path.substr(0, dot);
    const auto local = scope.locals.find(current);
    if (local == scope.locals.end()) {
        if (stmt != nullptr) {
            fail(stmt->location, "assignment through unknown local: " + current);
        }
        return {};
    }
    std::string type = local->second;
    size_t start = dot + 1;
    while (start < path.size()) {
        const size_t next = path.find('.', start);
        const std::string field =
            path.substr(start, next == std::string::npos ? next : next - start);
        const auto klass = scope.symbols.classes.find(base_type(type));
        if (klass == scope.symbols.classes.end()) {
            return {};
        }
        bool found = false;
        for (const FieldDecl& decl : klass->second->fields) {
            if (decl.name == field) {
                type = decl.type;
                found = true;
                break;
            }
        }
        if (!found) {
            if (stmt != nullptr) {
                fail(stmt->location, "unknown field: " + path);
            }
            return {};
        }
        if (next == std::string::npos) {
            return type;
        }
        start = next + 1;
    }
    return type;
}

std::string infer_expr(const FunctionScope& scope, std::string expr) {
    expr = trim(std::move(expr));
    if (expr.empty()) {
        return "void";
    }
    if (expr.size() > 1 && expr.front() == '*') {
        const std::string name = trim(expr.substr(1));
        if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
            std::string type = trim(local->second);
            if (!type.empty() && type.front() == '*') {
                return trim(type.substr(1));
            }
        }
    }
    const std::vector<std::string> tuple_parts = split_top_level(expr);
    if (tuple_parts.size() > 1) {
        std::ostringstream out;
        out << "tuple[";
        for (size_t i = 0; i < tuple_parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << infer_expr(scope, tuple_parts[i]);
        }
        out << "]";
        return out.str();
    }
    if (expr == "True" || expr == "False" || expr.find("==") != std::string::npos ||
        expr.find("!=") != std::string::npos || expr.find("<") != std::string::npos ||
        expr.find(">") != std::string::npos) {
        return "bool";
    }
    if (std::isdigit(static_cast<unsigned char>(expr.front())) != 0) {
        return expr.find('.') == std::string::npos ? "i32" : "f64";
    }
    if (starts_with(expr, "Ok(") || starts_with(expr, "Err(")) {
        return {};
    }
    const size_t call = expr.find('(');
    if (call != std::string::npos && expr.back() == ')') {
        const std::string callee = trim(expr.substr(0, call));
        if (scope.symbols.classes.contains(callee)) {
            return callee;
        }
        if (const auto fn = scope.symbols.functions.find(callee);
            fn != scope.symbols.functions.end()) {
            return fn->second;
        }
    }
    const size_t op = expr.find_first_of("+-*/%");
    if (op != std::string::npos) {
        const std::string left = infer_expr(scope, expr.substr(0, op));
        return left.empty() ? infer_expr(scope, expr.substr(op + 1)) : left;
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos) {
        return member_path_type(scope, nullptr, expr);
    }
    if (const auto local = scope.locals.find(expr); local != scope.locals.end()) {
        return local->second;
    }
    return {};
}

bool is_all_caps_name(const std::string& name) {
    return !name.empty() && std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

void check_type_match(const FunctionScope& scope, const RawStmt& stmt, const std::string& expected,
                      const std::string& expr) {
    const std::string got = infer_expr(scope, expr);
    if (assignment_type_allowed(expected, expr, got)) {
        return;
    }
    fail(stmt.location, "cannot assign " + got + " to " + expected + " without an explicit cast");
}

std::string assign_target_type(const FunctionScope& scope, const RawStmt& stmt,
                               const std::string& lhs) {
    if (lhs.size() > 1 && lhs.front() == '*') {
        const std::string name = trim(lhs.substr(1));
        const auto local = scope.locals.find(name);
        if (local == scope.locals.end()) {
            fail(stmt.location, "assignment through unknown local: " + name);
        }
        std::string type = trim(local->second);
        if (type.empty() || type.front() != '*') {
            fail(stmt.location, "cannot dereference non-pointer: " + name);
        }
        return trim(type.substr(1));
    }
    if (lhs.find('.') == std::string::npos) {
        if (scope.constants.contains(lhs)) {
            fail(stmt.location, "cannot assign to constant: " + lhs);
        }
        const auto local = scope.locals.find(lhs);
        if (local == scope.locals.end()) {
            fail(stmt.location, "assignment to unknown local: " + lhs);
        }
        return local->second;
    }
    return member_path_type(scope, &stmt, lhs);
}

void check_stmt(FunctionScope& scope, const RawStmt& stmt, const std::string& return_type);

void check_block(FunctionScope& scope, const std::vector<RawStmt>& body,
                 const std::string& return_type) {
    for (const RawStmt& stmt : body) {
        check_stmt(scope, stmt, return_type);
    }
}

void check_stmt(FunctionScope& scope, const RawStmt& stmt, const std::string& return_type) {
    const std::string text = trim(stmt.text);
    check_local_address_escape(stmt, scope.locals);
    if (starts_with(text, "return")) {
        const std::string got = infer_expr(scope, text.substr(6));
        if (return_type != "void" && !got.empty() && got != "auto" && got != return_type) {
            fail(stmt.location, "return type mismatch: expected " + return_type + ", got " + got);
        }
        return;
    }
    if (starts_with(text, "cpp(")) {
        return;
    }
    if (starts_with(text, "if ") || starts_with(text, "elif ") || starts_with(text, "while ") ||
        text == "else:") {
        check_block(scope, stmt.children, return_type);
        return;
    }
    if (starts_with(text, "for ")) {
        FunctionScope nested = scope;
        const size_t colon = text.find(':');
        const size_t in_pos = text.find(" in ");
        if (colon != std::string::npos && in_pos != std::string::npos && colon < in_pos) {
            nested.locals[trim(text.substr(4, colon - 4))] =
                trim(text.substr(colon + 1, in_pos - colon - 1));
        }
        check_block(nested, stmt.children, return_type);
        return;
    }
    const size_t colon = find_top_level_char(text, ':');
    const size_t assign = find_top_level_char(text, '=');
    for (const char* op : {"+=", "-=", "*=", "/=", "%=", "^=", "&=", "|="}) {
        const size_t compound = text.find(op);
        if (compound != std::string::npos) {
            (void)assign_target_type(scope, stmt, trim(text.substr(0, compound)));
            return;
        }
    }
    if (colon != std::string::npos && (assign == std::string::npos || colon < assign)) {
        const std::string name = trim(text.substr(0, colon));
        const std::string type = trim(text.substr(colon + 1, assign - colon - 1));
        if (!known_type(scope.symbols, type)) {
            fail(stmt.location, "unknown local type: " + type);
        }
        if (assign != std::string::npos) {
            check_type_match(scope, stmt, type, text.substr(assign + 1));
        }
        scope.locals[name] = type;
        if (is_all_caps_name(name)) {
            scope.constants.insert(name);
        }
        return;
    }
    if (assign != std::string::npos && text.find("==") == std::string::npos) {
        const std::string lhs = trim(text.substr(0, assign));
        if (split_top_level(lhs).size() > 1) {
            const std::vector<std::string> names = split_top_level(lhs);
            const std::vector<std::string> types =
                tuple_types(scope.symbols, infer_expr(scope, text.substr(assign + 1)));
            if (names.size() != types.size()) {
                fail(stmt.location, "tuple destructuring count mismatch");
            }
            for (size_t i = 0; i < names.size(); ++i) {
                scope.locals[names[i]] = types[i];
            }
            return;
        }
        if (lhs.find('.') == std::string::npos && !starts_with(lhs, "*") &&
            !scope.locals.contains(lhs)) {
            const std::string inferred = infer_expr(scope, text.substr(assign + 1));
            scope.locals[lhs] = inferred.empty() ? "auto" : inferred;
            if (is_all_caps_name(lhs)) {
                scope.constants.insert(lhs);
            }
            return;
        }
        const std::string target_type = assign_target_type(scope, stmt, lhs);
        if (!target_type.empty()) {
            check_type_match(scope, stmt, target_type, text.substr(assign + 1));
        }
    }
}

void check_bodies(const ModuleAst& module, const Symbols& symbols) {
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            FunctionScope scope{symbols, {}, {}};
            for (const ParamDecl& param : method.params) {
                scope.locals[param.name] = param.type;
            }
            check_block(scope, method.body,
                        method.return_type.empty() ? "void" : method.return_type);
            if (!method.return_type.empty() && method.return_type != "void" &&
                !block_guarantees_return(method.body)) {
                fail(method.location, "missing return in function: " + method.name);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        FunctionScope scope{symbols, {}, {}};
        for (const ParamDecl& param : fn.params) {
            scope.locals[param.name] = param.type;
        }
        check_block(scope, fn.body, fn.return_type.empty() ? "void" : fn.return_type);
        if (!fn.return_type.empty() && fn.return_type != "void" &&
            !block_guarantees_return(fn.body)) {
            fail(fn.location, "missing return in function: " + fn.name);
        }
    }
}

} // namespace

void analyze_module(const ModuleAst& module, SemanticOptions options) {
    const Symbols symbols = collect_symbols(module);
    check_build_flags(module);
    check_naming(module);
    check_unsupported_python(module);
    check_declarations(module, symbols);
    if (options.check_bodies) {
        check_bodies(module, symbols);
    }
}

} // namespace dudu
