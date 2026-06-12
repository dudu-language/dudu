#include "dudu/sema.hpp"

#include "dudu/naming.hpp"

#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace dudu {
namespace {

std::string trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool is_builtin_type(const std::string& type) {
    static const std::set<std::string> builtins = {"bool", "i8",   "i16", "i32",   "i64",   "u8",
                                                   "u16",  "u32",  "u64", "isize", "usize", "f32",
                                                   "f64",  "void", "str", "cstr"};
    return builtins.contains(type);
}

std::string base_type(std::string type) {
    type = trim(std::move(type));
    while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
        type = trim(type.substr(1));
    }
    const size_t bracket = type.find('[');
    if (bracket != std::string::npos) {
        return trim(type.substr(0, bracket));
    }
    return type;
}

struct Symbols {
    std::set<std::string> types;
    std::map<std::string, std::string> functions;
    std::map<std::string, const ClassDecl*> classes;
};

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

void add_name(std::map<std::string, SourceLocation>& names, const std::string& name,
              const SourceLocation& location) {
    const auto [it, inserted] = names.emplace(name, location);
    if (!inserted) {
        fail(location, "duplicate declaration: " + name);
    }
}

bool known_type(const Symbols& symbols, const std::string& type) {
    if (starts_with(trim(type), "fn(")) {
        return true;
    }
    const std::string base = base_type(type);
    return base.empty() || is_builtin_type(base) || symbols.types.contains(base) ||
           base.find('.') != std::string::npos || starts_with(base, "struct ") || base == "list" ||
           base == "dict" || base == "set" || base == "tuple" || base == "Result" ||
           base == "Option" || base == "fn" || base == "const" || base == "atomic" ||
           base == "volatile" || base == "storage" || base == "shared" || base == "device";
}

std::vector<std::string> split_top_level(std::string text) {
    std::vector<std::string> out;
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t start = 0;
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
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            --depth;
        } else if (c == ',' && depth == 0) {
            out.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(text.substr(start)));
    return out;
}

size_t find_top_level_char(const std::string& text, char wanted) {
    int depth = 0;
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
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            --depth;
        } else if (c == wanted && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

Symbols collect_symbols(const ModuleAst& module) {
    Symbols symbols;
    std::map<std::string, SourceLocation> names;
    for (const char* type : {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize",
                             "usize", "f32", "f64", "void", "str", "cstr"}) {
        symbols.types.insert(type);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        add_name(names, alias.name, alias.location);
        symbols.types.insert(alias.name);
    }
    for (const EnumDecl& en : module.enums) {
        add_name(names, en.name, en.location);
        symbols.types.insert(en.name);
    }
    for (const ClassDecl& klass : module.classes) {
        add_name(names, klass.name, klass.location);
        symbols.types.insert(klass.name);
        symbols.classes[klass.name] = &klass;
    }
    for (const FunctionDecl& fn : module.functions) {
        add_name(names, fn.name, fn.location);
        symbols.functions[fn.name] = fn.return_type.empty() ? "void" : fn.return_type;
    }
    for (const ConstDecl& constant : module.constants) {
        add_name(names, constant.name, constant.location);
    }
    return symbols;
}

void check_declarations(const ModuleAst& module, const Symbols& symbols) {
    for (const ClassDecl& klass : module.classes) {
        std::set<std::string> fields;
        for (const FieldDecl& field : klass.fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate field: " + field.name);
            }
            if (!known_type(symbols, field.type)) {
                fail(field.location, "unknown field type: " + field.type);
            }
        }
        for (const FunctionDecl& method : klass.methods) {
            std::set<std::string> params;
            for (const ParamDecl& param : method.params) {
                if (!params.insert(param.name).second) {
                    fail(param.location, "duplicate parameter: " + param.name);
                }
                if (!known_type(symbols, param.type)) {
                    fail(param.location, "unknown parameter type: " + param.type);
                }
            }
            if (!known_type(symbols, method.return_type.empty() ? "void" : method.return_type)) {
                fail(method.location, "unknown return type: " + method.return_type);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        std::set<std::string> params;
        for (const ParamDecl& param : fn.params) {
            if (!params.insert(param.name).second) {
                fail(param.location, "duplicate parameter: " + param.name);
            }
            if (!known_type(symbols, param.type)) {
                fail(param.location, "unknown parameter type: " + param.type);
            }
        }
        if (!known_type(symbols, fn.return_type.empty() ? "void" : fn.return_type)) {
            fail(fn.location, "unknown return type: " + fn.return_type);
        }
    }
}

struct FunctionScope {
    const Symbols& symbols;
    std::map<std::string, std::string> locals;
};

std::string infer_expr(const FunctionScope& scope, std::string expr) {
    expr = trim(std::move(expr));
    if (expr.empty()) {
        return "void";
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
        const std::string base = expr.substr(0, dot);
        const auto local = scope.locals.find(base);
        if (local != scope.locals.end()) {
            const auto klass = scope.symbols.classes.find(base_type(local->second));
            if (klass != scope.symbols.classes.end()) {
                const std::string field = expr.substr(dot + 1);
                for (const FieldDecl& decl : klass->second->fields) {
                    if (decl.name == field) {
                        return decl.type;
                    }
                }
            }
        }
        return {};
    }
    if (const auto local = scope.locals.find(expr); local != scope.locals.end()) {
        return local->second;
    }
    return {};
}

std::vector<std::string> tuple_types(std::string type) {
    type = trim(std::move(type));
    if (!starts_with(type, "tuple[") || type.back() != ']') {
        return {};
    }
    return split_top_level(type.substr(6, type.size() - 7));
}

void check_assign_target(const FunctionScope& scope, const RawStmt& stmt, const std::string& lhs) {
    if (lhs.find('.') == std::string::npos) {
        if (!scope.locals.contains(lhs)) {
            fail(stmt.location, "assignment to unknown local: " + lhs);
        }
        return;
    }
    const size_t dot = lhs.find('.');
    const std::string base = lhs.substr(0, dot);
    const auto local = scope.locals.find(base);
    if (local == scope.locals.end()) {
        fail(stmt.location, "assignment through unknown local: " + base);
    }
    const auto klass = scope.symbols.classes.find(local->second);
    if (klass == scope.symbols.classes.end()) {
        return;
    }
    const std::string field = lhs.substr(dot + 1);
    for (const FieldDecl& decl : klass->second->fields) {
        if (decl.name == field) {
            return;
        }
    }
    fail(stmt.location, "unknown field: " + lhs);
}

void check_stmt(FunctionScope& scope, const RawStmt& stmt, const std::string& return_type);
bool block_guarantees_return(const std::vector<RawStmt>& body);

void check_block(FunctionScope& scope, const std::vector<RawStmt>& body,
                 const std::string& return_type) {
    for (const RawStmt& stmt : body) {
        check_stmt(scope, stmt, return_type);
    }
}

void check_stmt(FunctionScope& scope, const RawStmt& stmt, const std::string& return_type) {
    const std::string text = trim(stmt.text);
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
            check_assign_target(scope, stmt, trim(text.substr(0, compound)));
            return;
        }
    }
    if (colon != std::string::npos && (assign == std::string::npos || colon < assign)) {
        const std::string name = trim(text.substr(0, colon));
        const std::string type = trim(text.substr(colon + 1, assign - colon - 1));
        if (!known_type(scope.symbols, type)) {
            fail(stmt.location, "unknown local type: " + type);
        }
        scope.locals[name] = type;
        return;
    }
    if (assign != std::string::npos && text.find("==") == std::string::npos) {
        const std::string lhs = trim(text.substr(0, assign));
        if (split_top_level(lhs).size() > 1) {
            const std::vector<std::string> names = split_top_level(lhs);
            const std::vector<std::string> types =
                tuple_types(infer_expr(scope, text.substr(assign + 1)));
            if (names.size() != types.size()) {
                fail(stmt.location, "tuple destructuring count mismatch");
            }
            for (size_t i = 0; i < names.size(); ++i) {
                scope.locals[names[i]] = types[i];
            }
            return;
        }
        if (lhs.find('.') == std::string::npos && !scope.locals.contains(lhs)) {
            const std::string inferred = infer_expr(scope, text.substr(assign + 1));
            scope.locals[lhs] = inferred.empty() ? "auto" : inferred;
            return;
        }
        check_assign_target(scope, stmt, lhs);
    }
}

bool branch_chain_guarantees_return(const std::vector<RawStmt>& body, size_t& index) {
    bool has_else = false;
    bool all_branches_return = block_guarantees_return(body[index].children);
    while (index + 1 < body.size()) {
        const std::string next = trim(body[index + 1].text);
        if (!starts_with(next, "elif ") && next != "else:") {
            break;
        }
        ++index;
        has_else = has_else || next == "else:";
        all_branches_return = all_branches_return && block_guarantees_return(body[index].children);
        if (has_else) {
            break;
        }
    }
    return has_else && all_branches_return;
}

bool block_guarantees_return(const std::vector<RawStmt>& body) {
    for (size_t i = 0; i < body.size(); ++i) {
        const std::string text = trim(body[i].text);
        if (starts_with(text, "return")) {
            return true;
        }
        if (starts_with(text, "if ") && branch_chain_guarantees_return(body, i)) {
            return true;
        }
    }
    return false;
}

void check_bodies(const ModuleAst& module, const Symbols& symbols) {
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            FunctionScope scope{symbols, {}};
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
        FunctionScope scope{symbols, {}};
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
    check_naming(module);
    check_declarations(module, symbols);
    if (options.check_bodies) {
        check_bodies(module, symbols);
    }
}

} // namespace dudu
