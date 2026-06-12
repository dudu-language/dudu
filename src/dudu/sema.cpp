#include "dudu/sema.hpp"

#include "dudu/build_flags.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/escapes.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_constexpr.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/type_compat.hpp"
#include "dudu/unsupported.hpp"

#include <cctype>
#include <map>
#include <set>
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
        if (const auto local = scope.locals.find(path); local != scope.locals.end())
            return local->second;
        return {};
    }
    std::string current = path.substr(0, dot);
    const auto local = scope.locals.find(current);
    if (local == scope.locals.end()) {
        if (stmt != nullptr)
            fail(stmt->location, "assignment through unknown local: " + current);
        return {};
    }
    std::string type = local->second;
    size_t start = dot + 1;
    while (start < path.size()) {
        const size_t next = path.find('.', start);
        const std::string field =
            path.substr(start, next == std::string::npos ? next : next - start);
        const auto klass = scope.symbols.classes.find(base_type(type));
        if (klass == scope.symbols.classes.end())
            return {};
        bool found = false;
        for (const FieldDecl& decl : klass->second->fields) {
            if (decl.name == field) {
                type = decl.type;
                found = true;
                break;
            }
        }
        if (!found) {
            if (stmt != nullptr)
                fail(stmt->location, "unknown field: " + path);
            return {};
        }
        if (next == std::string::npos)
            return type;
        start = next + 1;
    }
    return type;
}
std::string infer_expr(const FunctionScope& scope, std::string expr,
                       const SourceLocation* location = nullptr);
std::vector<std::string> call_args(std::string expr, size_t open) {
    std::string args = trim(expr.substr(open + 1, expr.size() - open - 2));
    return args.empty() ? std::vector<std::string>{} : split_top_level_args(args);
}
bool can_assign_expr(const FunctionScope& scope, const std::string& expected,
                     const std::string& expr, const std::string& got) {
    return assignment_type_allowed(expected, expr, got) ||
           assignment_type_allowed(resolve_alias(scope.symbols, expected), expr,
                                   resolve_alias(scope.symbols, got));
}
bool is_builtin_call(const std::string& callee) {
    static const std::set<std::string> builtins = {"align_up", "delete", "free",  "len",
                                                   "max",      "min",    "print", "range"};
    return builtins.contains(callee);
}
void check_call_args(const FunctionScope& scope, const std::string& callee,
                     const FunctionSignature& signature, const std::vector<std::string>& args,
                     const SourceLocation* location) {
    if (location == nullptr)
        return;
    if (args.size() != signature.params.size()) {
        fail(*location, "function " + callee + " expects " +
                            std::to_string(signature.params.size()) + " arguments, got " +
                            std::to_string(args.size()));
    }
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string got = infer_expr(scope, args[i], location);
        const std::string& expected = signature.params[i];
        if (!can_assign_expr(scope, expected, args[i], got)) {
            fail(*location, "argument " + std::to_string(i + 1) + " for " + callee + " expects " +
                                expected + ", got " + got);
        }
    }
}
void check_constructor_args(const FunctionScope& scope, const ClassDecl& klass,
                            const std::vector<std::string>& args, const SourceLocation* location) {
    if (location == nullptr)
        return;
    std::set<std::string> named_fields;
    size_t positional = 0;
    for (const std::string& arg : args) {
        const size_t equal = find_top_level_char(arg, '=');
        if (equal == std::string::npos) {
            if (!named_fields.empty()) {
                fail(*location,
                     "positional constructor argument after named fields: " + klass.name);
            }
            if (positional >= klass.fields.size()) {
                fail(*location, "constructor " + klass.name + " expects at most " +
                                    std::to_string(klass.fields.size()) + " arguments, got " +
                                    std::to_string(args.size()));
            }
            const FieldDecl& field = klass.fields[positional];
            const std::string got = infer_expr(scope, arg, location);
            if (!can_assign_expr(scope, field.type, arg, got)) {
                fail(*location, "constructor " + klass.name + " argument " +
                                    std::to_string(positional + 1) + " expects " + field.type +
                                    ", got " + got);
            }
            ++positional;
            continue;
        }
        const std::string name = trim(arg.substr(0, equal));
        if (!named_fields.insert(name).second)
            fail(*location, "duplicate constructor field: " + name);
        const FieldDecl* field = nullptr;
        for (const FieldDecl& candidate : klass.fields) {
            if (candidate.name == name) {
                field = &candidate;
                break;
            }
        }
        if (field == nullptr)
            fail(*location, "unknown constructor field: " + klass.name + "." + name);
        const std::string value = arg.substr(equal + 1);
        const std::string got = infer_expr(scope, value, location);
        if (!can_assign_expr(scope, field->type, value, got)) {
            fail(*location, "constructor field " + klass.name + "." + name + " expects " +
                                field->type + ", got " + got);
        }
    }
}
std::string infer_expr(const FunctionScope& scope, std::string expr,
                       const SourceLocation* location) {
    expr = trim(std::move(expr));
    if (expr.empty())
        return "void";
    if (starts_with(expr, "{") && expr.back() == '}') {
        for (const std::string& entry : split_top_level(expr.substr(1, expr.size() - 2))) {
            if (find_top_level_char(entry, ':') != std::string::npos)
                return "dict";
        }
        return "set";
    }
    if (starts_with(expr, "lambda "))
        return "lambda";
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
            out << infer_expr(scope, tuple_parts[i], location);
        }
        out << "]";
        return out.str();
    }
    if (expr == "True" || expr == "False" || expr.find("==") != std::string::npos ||
        expr.find("!=") != std::string::npos || expr.find("<") != std::string::npos ||
        expr.find(">") != std::string::npos) {
        return "bool";
    }
    const size_t op = find_top_level_operator(expr);
    if (op != std::string::npos) {
        const std::string left = infer_expr(scope, expr.substr(0, op), location);
        return left.empty() ? infer_expr(scope, expr.substr(op + 1), location) : left;
    }
    if (std::isdigit(static_cast<unsigned char>(expr.front())) != 0) {
        return expr.find('.') == std::string::npos ? "i32" : "f64";
    }
    if (expr == "None") {
        return "None";
    }
    const size_t call = find_call_open(expr);
    if (call != std::string::npos && expr.back() == ')') {
        const std::string callee = trim(expr.substr(0, call));
        if (const auto type =
                infer_allocation_call(scope.symbols, location, callee, call_args(expr, call)))
            return *type;
        if (is_deallocation_call(callee)) {
            std::vector<std::string> types;
            for (const std::string& arg : call_args(expr, call))
                types.push_back(infer_expr(scope, arg, location));
            if (location != nullptr)
                check_deallocation_args(*location, callee, types);
            return "void";
        }
        if (callee == "Ok" || callee == "Err") {
            const std::vector<std::string> args = call_args(expr, call);
            if (args.size() != 1 && location != nullptr) {
                fail(*location, callee + " expects 1 argument, got " + std::to_string(args.size()));
            }
            return callee + "[" +
                   (args.size() == 1 ? infer_expr(scope, args.front(), location) : "") + "]";
        }
        if (const auto klass = scope.symbols.classes.find(callee);
            klass != scope.symbols.classes.end()) {
            check_constructor_args(scope, *klass->second, call_args(expr, call), location);
            return callee;
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args(scope, callee, fn->second, call_args(expr, call), location);
            return fn->second.return_type;
        }
        if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
            FunctionSignature signature;
            if (parse_function_type(resolve_alias(scope.symbols, local->second), signature)) {
                check_call_args(scope, callee, signature, call_args(expr, call), location);
                return signature.return_type;
            }
        }
        if (location != nullptr && callee.find('.') == std::string::npos &&
            callee.find('[') == std::string::npos && is_plain_identifier(callee) &&
            !known_type(scope.symbols, callee) && !is_builtin_call(callee)) {
            fail(*location, "unknown function: " + callee);
        }
    }
    const size_t index = expr.find('[');
    if (location != nullptr && index != std::string::npos && expr.back() == ']') {
        const std::string name = trim(expr.substr(0, index));
        if (is_plain_identifier(name)) {
            return indexed_value_type(scope.symbols, scope.locals, *location, name,
                                      "indexed access to unknown local: ");
        }
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos) {
        return member_path_type(scope, nullptr, expr);
    }
    if (const auto local = scope.locals.find(expr); local != scope.locals.end()) {
        return local->second;
    }
    if (const auto fn = scope.symbols.function_signatures.find(expr);
        fn != scope.symbols.function_signatures.end()) {
        return function_type(fn->second);
    }
    if (location != nullptr && is_plain_identifier(expr)) {
        fail(*location, "unknown identifier: " + expr);
    }
    return {};
}
void check_type_match(const FunctionScope& scope, const RawStmt& stmt, const std::string& expected,
                      const std::string& expr) {
    const std::string got = infer_expr(scope, expr, &stmt.location);
    if (can_assign_expr(scope, expected, expr, got)) {
        return;
    }
    fail(stmt.location, "cannot assign " + got + " to " + expected + " without an explicit cast");
}
void check_condition_type(const FunctionScope& scope, const RawStmt& stmt, std::string expr) {
    expr = trim(std::move(expr));
    if (!expr.empty() && expr.back() == ':') {
        expr.pop_back();
    }
    const std::string got = infer_expr(scope, expr, &stmt.location);
    if (!got.empty() && got != "bool") {
        fail(stmt.location, "condition must be bool, got " + got);
    }
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
        const size_t index = lhs.find('[');
        if (index != std::string::npos) {
            const std::string name = trim(lhs.substr(0, index));
            return indexed_value_type(scope.symbols, scope.locals, stmt.location, name,
                                      "indexed assignment to unknown local: ");
        }
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
void check_stmt(FunctionScope& scope, const RawStmt& stmt, const std::string& return_type,
                int loop_depth);
void check_block(FunctionScope& scope, const std::vector<RawStmt>& body,
                 const std::string& return_type, int loop_depth) {
    for (const RawStmt& stmt : body) {
        check_stmt(scope, stmt, return_type, loop_depth);
    }
}
void check_stmt(FunctionScope& scope, const RawStmt& stmt, const std::string& return_type,
                int loop_depth) {
    const std::string text = trim(stmt.text);
    check_local_address_escape(stmt, scope.locals);
    if (starts_with(text, "return")) {
        const std::string expr = text.substr(6);
        const std::string got = infer_expr(scope, expr, &stmt.location);
        if (return_type == "void" && got != "void")
            fail(stmt.location, "void function cannot return " + got);
        if (return_type != "void" && !can_assign_expr(scope, return_type, expr, got)) {
            fail(stmt.location, "return type mismatch: expected " + return_type + ", got " + got);
        }
        return;
    }
    if (starts_with(text, "cpp(") || text == "pass")
        return;
    if (starts_with(text, "delete ")) {
        check_deallocation_args(stmt.location, "delete",
                                {infer_expr(scope, text.substr(7), &stmt.location)});
        return;
    }
    if ((text == "break" || text == "continue") && loop_depth == 0) {
        fail(stmt.location, text + " outside loop");
    }
    if (text == "break" || text == "continue") {
        return;
    }
    if (starts_with(text, "if ")) {
        check_condition_type(scope, stmt, text.substr(3));
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (starts_with(text, "elif ")) {
        check_condition_type(scope, stmt, text.substr(5));
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (text == "else:") {
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (starts_with(text, "while ")) {
        check_condition_type(scope, stmt, text.substr(6));
        check_block(scope, stmt.children, return_type, loop_depth + 1);
        return;
    }
    if (starts_with(text, "for ")) {
        FunctionScope nested = scope;
        const size_t colon = text.find(':');
        const size_t in_pos = text.find(" in ");
        if (colon != std::string::npos && in_pos != std::string::npos && colon < in_pos) {
            const std::string name = trim(text.substr(4, colon - 4));
            std::string iterable = trim(text.substr(in_pos + 4));
            if (!iterable.empty() && iterable.back() == ':') {
                iterable.pop_back();
            }
            check_local_binding_name(stmt.location, name);
            const std::string type = trim(text.substr(colon + 1, in_pos - colon - 1));
            check_iterable_binding(scope.symbols, scope.locals, stmt.location, type, iterable);
            nested.locals[name] = type;
        }
        check_block(nested, stmt.children, return_type, loop_depth + 1);
        return;
    }
    const size_t colon = find_top_level_char(text, ':');
    const size_t assign = find_top_level_char(text, '=');
    const size_t compound = compound_assign_pos(text, assign);
    if (compound != std::string::npos) {
        (void)assign_target_type(scope, stmt, trim(text.substr(0, compound)));
        return;
    }
    if (colon != std::string::npos && (assign == std::string::npos || colon < assign)) {
        const std::string name = trim(text.substr(0, colon));
        const std::string type = trim(text.substr(colon + 1, assign - colon - 1));
        check_local_binding_name(stmt.location, name);
        if (!known_type(scope.symbols, type)) {
            fail(stmt.location, "unknown local type: " + type);
        }
        if (assign != std::string::npos) {
            check_type_match(scope, stmt, type, text.substr(assign + 1));
        }
        scope.locals[name] = type;
        if (is_dudu_all_caps(name)) {
            scope.constants.insert(name);
        }
        return;
    }
    if (assign != std::string::npos && text.find("==") == std::string::npos) {
        const std::string lhs = trim(text.substr(0, assign));
        if (split_top_level(lhs).size() > 1) {
            const std::vector<std::string> names = split_top_level(lhs);
            const std::vector<std::string> types = tuple_types(
                scope.symbols, infer_expr(scope, text.substr(assign + 1), &stmt.location));
            if (names.size() != types.size()) {
                fail(stmt.location, "tuple destructuring count mismatch");
            }
            check_destructure_bindings(stmt.location, names, scope.locals);
            for (size_t i = 0; i < names.size(); ++i) {
                scope.locals[names[i]] = types[i];
            }
            return;
        }
        if (lhs.find('.') == std::string::npos && !starts_with(lhs, "*") &&
            lhs.find('[') == std::string::npos && !scope.locals.contains(lhs)) {
            check_local_binding_name(stmt.location, lhs);
            const std::string inferred = infer_expr(scope, text.substr(assign + 1), &stmt.location);
            scope.locals[lhs] = inferred.empty() ? "auto" : inferred;
            if (is_dudu_all_caps(lhs)) {
                scope.constants.insert(lhs);
            }
            return;
        }
        const std::string target_type = assign_target_type(scope, stmt, lhs);
        if (!target_type.empty()) {
            check_type_match(scope, stmt, target_type, text.substr(assign + 1));
        }
        return;
    }
    (void)infer_expr(scope, text, &stmt.location);
}
void check_bodies(const ModuleAst& module, const Symbols& symbols) {
    FunctionScope base{symbols, {}, {}};
    for (const ConstDecl& constant : module.constants) {
        base.locals[constant.name] = constant.type;
        base.constants.insert(constant.name);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            FunctionScope scope = base;
            for (const ParamDecl& param : method.params) {
                scope.locals[param.name] = param.type;
            }
            check_block(scope, method.body,
                        method.return_type.empty() ? "void" : method.return_type, 0);
            if (!method.return_type.empty() && method.return_type != "void" &&
                !block_guarantees_return(method.body)) {
                fail(method.location, "missing return in function: " + method.name);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        FunctionScope scope = base;
        for (const ParamDecl& param : fn.params) {
            scope.locals[param.name] = param.type;
        }
        check_block(scope, fn.body, fn.return_type.empty() ? "void" : fn.return_type, 0);
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
    check_constexpr_uses(module);
    if (options.check_bodies)
        check_bodies(module, symbols);
}
} // namespace dudu
