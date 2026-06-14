#include "dudu/cpp_stmt_emit.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_pointer_members.hpp"
#include "dudu/cpp_stmt_types.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

std::string indent(int depth) {
    return std::string(static_cast<size_t>(depth) * 4, ' ');
}

std::string lower_expr(std::string expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals) {
    return lower_cpp_expr(rewrite_pointer_members(std::move(expr), locals), aliases);
}

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals);

std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals,
                               std::string_view separator = ", ") {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << lower_expr(exprs[i], aliases, locals);
    }
    return out.str();
}

std::string join_names(const std::vector<std::string>& names) {
    std::ostringstream out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << names[i];
    }
    return out.str();
}

bool has_expr(const Expr& expr) {
    return !expr.text.empty();
}

std::string join_lowered_template_args(const std::vector<Expr>& exprs,
                                       const std::vector<std::string>& aliases) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_template_call_arg(exprs[i].text, aliases);
    }
    return out.str();
}

std::string join_lowered_type_args(const std::vector<TypeRef>& types,
                                   const std::vector<std::string>& aliases) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(types[i], aliases);
    }
    return out.str();
}

std::string join_type_arg_texts(const std::vector<TypeRef>& types) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << types[i].text;
    }
    return out.str();
}

std::string join_template_arg_texts(const std::vector<Expr>& exprs) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << exprs[i].text;
    }
    return out.str();
}

std::string cpp_binary_operator(const std::string& op) {
    if (op == "and") {
        return "&&";
    }
    if (op == "or") {
        return "||";
    }
    return op;
}

bool has_named_argument_shape(const std::vector<Expr>& args) {
    for (const Expr& arg : args) {
        if (arg.kind == ExprKind::NamedArg) {
            return true;
        }
    }
    return false;
}

bool is_builtin_cast_call(std::string_view name) {
    static const std::vector<std::string_view> types = {"bool", "i8",  "i16", "i32", "i64",
                                                        "u8",   "u16", "u32", "u64", "usize",
                                                        "f32",  "f64", "str", "cstr"};
    return std::find(types.begin(), types.end(), name) != types.end();
}

bool is_pointer_cast_type_like(const std::string& type) {
    const std::string base =
        type.find('[') == std::string::npos ? type : type.substr(0, type.find('['));
    return is_builtin_cast_call(type) || starts_with(type, "struct ") ||
           type.find('[') != std::string::npos || type.find('.') != std::string::npos ||
           (!base.empty() && std::isupper(static_cast<unsigned char>(base.front())) != 0);
}

std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const std::map<std::string, std::string>& locals) {
    if (!expr.callee.empty()) {
        return lower_expr(expr.callee.front(), aliases, locals);
    }
    return lower_expr(expr.name, aliases, locals);
}

bool is_pointer_type(std::string type) {
    type = trim_copy(std::move(type));
    return !type.empty() && type.front() == '*';
}

bool is_pointer_list_type(std::string type) {
    type = trim_copy(std::move(type));
    return starts_with(type, "list[*") && ends_with(type, "]");
}

bool is_pointer_receiver_expr(const Expr& expr, const std::map<std::string, std::string>& locals) {
    if (expr.kind == ExprKind::Name) {
        const auto local = locals.find(expr.name);
        return local != locals.end() && is_pointer_type(local->second);
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2 &&
        expr.children.front().kind == ExprKind::Name) {
        const auto local = locals.find(expr.children.front().name);
        return local != locals.end() && is_pointer_list_type(local->second);
    }
    return false;
}

std::string unquoted_string_literal(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const std::map<std::string, std::string>& locals) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        return expr.name;
    }
    if (expr.kind == ExprKind::StringLiteral) {
        return unquoted_string_literal(expr.text);
    }
    if (expr.kind == ExprKind::Member) {
        if (const std::optional<std::string> path = member_path_from_expr(expr)) {
            return *path;
        }
    }
    return lower_expr(expr, aliases, locals);
}

std::optional<std::string>
lower_pointer_cast_expr(const Expr& expr, const std::vector<std::string>& aliases,
                        const std::map<std::string, std::string>& locals) {
    if (expr.op != "*" || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Call) {
        return std::nullopt;
    }
    const Expr& call = expr.children.front();
    const std::string type_name = call_callee_text(call);
    if (!is_pointer_cast_type_like(type_name)) {
        return std::nullopt;
    }
    return "reinterpret_cast<" + lower_cpp_type("*" + type_name, aliases) + ">(" +
           join_lowered_exprs(call.children, aliases, locals) + ")";
}

std::string lower_named_argument_call(const Expr& expr, const std::vector<std::string>& aliases,
                                      const std::map<std::string, std::string>& locals) {
    std::ostringstream out;
    out << lower_callee_expr(expr, aliases, locals) << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (expr.children[i].kind == ExprKind::NamedArg && expr.children[i].children.size() == 1) {
            out << "." << expr.children[i].name << " = "
                << lower_expr(expr.children[i].children.front(), aliases, locals);
            continue;
        }
        out << lower_expr(expr.children[i], aliases, locals);
    }
    out << "}";
    return out.str();
}

std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals) {
    if (has_named_argument_shape(expr.children)) {
        return lower_named_argument_call(expr, aliases, locals);
    }
    const std::string callee_name = call_callee_text(expr);
    if (starts_with(callee_name, "*")) {
        const std::string type = trim_copy(callee_name.substr(1));
        if (is_pointer_cast_type_like(type)) {
            return "reinterpret_cast<" + lower_cpp_type("*" + type, aliases) + ">(" +
                   join_lowered_exprs(expr.children, aliases, locals) + ")";
        }
    }
    if (expr.name == "len" && expr.children.size() == 1) {
        return "(" + lower_expr(expr.children.front(), aliases, locals) + ").size()";
    }
    if (expr.name == "str" && expr.children.size() == 1) {
        if (expr.children.front().kind == ExprKind::StringLiteral) {
            return "std::string(" + lower_expr(expr.children.front(), aliases, locals) + ")";
        }
        return "std::to_string(" + lower_expr(expr.children.front(), aliases, locals) + ")";
    }
    if (is_builtin_cast_call(expr.name)) {
        return lower_cpp_type(expr.name, aliases) + "(" +
               join_lowered_exprs(expr.children, aliases, locals) + ")";
    }
    std::string callee = lower_callee_expr(expr, aliases, locals);
    if (ends_with(callee, ".append")) {
        callee.replace(callee.size() - 7, 7, ".push_back");
    }
    return callee + "(" + join_lowered_exprs(expr.children, aliases, locals) + ")";
}

std::string lower_lambda_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const std::map<std::string, std::string>& locals) {
    if (expr.children.size() != 1) {
        return lower_expr(expr.text, aliases, locals);
    }
    std::ostringstream out;
    out << "[&](";
    for (size_t i = 0; i < expr.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (expr.params[i].kind == ExprKind::Name && !expr.params[i].name.empty()) {
            out << "auto&& " << expr.params[i].name;
        } else {
            out << "auto&& " << lower_expr(expr.params[i], aliases, locals);
        }
    }
    out << ") { return " << lower_expr(expr.children.front(), aliases, locals) << "; }";
    return out.str();
}

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals) {
    if (expr.text.empty() || expr.kind == ExprKind::Unknown) {
        return lower_expr(expr.text, aliases, locals);
    }
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        return expr.text == "True" ? "true" : "false";
    case ExprKind::NoneLiteral:
        return "nullptr";
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
        return lower_numeric_separators(expr.text);
    case ExprKind::Name:
    case ExprKind::CppEscape:
        return lower_expr(expr.text, aliases, locals);
    case ExprKind::StringLiteral:
        return expr.text;
    case ExprKind::Unary:
        if (const auto pointer_cast = lower_pointer_cast_expr(expr, aliases, locals)) {
            return *pointer_cast;
        }
        if (expr.children.size() == 1) {
            const std::string op = expr.op == "not" ? "!" : expr.op;
            return "(" + op + lower_expr(expr.children.front(), aliases, locals) + ")";
        }
        break;
    case ExprKind::Binary:
        if (expr.children.size() == 2 && !trim_copy(expr.children[0].text).empty() &&
            !trim_copy(expr.children[1].text).empty()) {
            return "(" + lower_expr(expr.children[0], aliases, locals) + " " +
                   cpp_binary_operator(expr.op) + " " +
                   lower_expr(expr.children[1], aliases, locals) + ")";
        }
        break;
    case ExprKind::Conditional:
        if (expr.children.size() == 3) {
            return "(" + lower_expr(expr.children[1], aliases, locals) + " ? " +
                   lower_expr(expr.children[0], aliases, locals) + " : " +
                   lower_expr(expr.children[2], aliases, locals) + ")";
        }
        break;
    case ExprKind::Call:
        return lower_call_expr(expr, aliases, locals);
    case ExprKind::TemplateCall: {
        if (expr.template_args.empty() && expr.template_type_args.empty()) {
            break;
        }
        const std::string lowered_template_args =
            !expr.template_type_args.empty()
                ? join_lowered_type_args(expr.template_type_args, aliases)
                : join_lowered_template_args(expr.template_args, aliases);
        const std::string lowered_call_args = join_lowered_exprs(expr.children, aliases, locals);
        if (expr.name == "new") {
            return "new " + lowered_template_args + "(" + lowered_call_args + ")";
        }
        if (expr.name == "malloc") {
            const std::string type = lowered_template_args;
            return "static_cast<" + type + "*>(std::malloc(sizeof(" + type + ") * (" +
                   lowered_call_args + ")))";
        }
        if (starts_with(expr.name, "*")) {
            const std::string pointee = !expr.template_type_args.empty()
                                            ? trim_copy(expr.name.substr(1)) + "[" +
                                                  join_type_arg_texts(expr.template_type_args) + "]"
                                            : trim_copy(expr.name.substr(1)) + "[" +
                                                  join_template_arg_texts(expr.template_args) + "]";
            return "reinterpret_cast<" + lower_cpp_type("*" + pointee, aliases) + ">(" +
                   lowered_call_args + ")";
        }
        if (expr.name == "sizeof" || expr.name == "alignof") {
            return expr.name + "(" + lowered_template_args + ")";
        }
        if (expr.name == "offsetof" && expr.children.size() == 1) {
            return "offsetof(" + lowered_template_args + ", " +
                   lower_offsetof_field(expr.children.front(), aliases, locals) + ")";
        }
        if ((expr.name == "list" || expr.name == "dict" || expr.name == "set") &&
            expr.children.empty()) {
            const std::string type_args = !expr.template_type_args.empty()
                                              ? join_type_arg_texts(expr.template_type_args)
                                              : join_template_arg_texts(expr.template_args);
            return lower_cpp_type(expr.name + "[" + type_args + "]", aliases) + "{}";
        }
        return lower_callee_expr(expr, aliases, locals) + "<" + lowered_template_args + ">(" +
               lowered_call_args + ")";
    }
    case ExprKind::Member:
        if (expr.children.size() == 1) {
            if (is_pointer_receiver_expr(expr.children.front(), locals)) {
                return lower_expr(expr.children.front(), aliases, locals) + "->" + expr.name;
            }
            return lower_expr(lower_expr(expr.children.front(), aliases, locals) + "." + expr.name,
                              aliases, locals);
        }
        break;
    case ExprKind::DictEntry:
        if (expr.children.size() == 2) {
            return "{" + lower_expr(expr.children[0], aliases, locals) + ", " +
                   lower_expr(expr.children[1], aliases, locals) + "}";
        }
        break;
    case ExprKind::NamedArg:
        if (expr.children.size() == 1) {
            return "." + expr.name + " = " + lower_expr(expr.children.front(), aliases, locals);
        }
        break;
    case ExprKind::Slice:
        break;
    case ExprKind::DictLiteral:
        return "{" + join_lowered_exprs(expr.children, aliases, locals) + "}";
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            std::string out = lower_expr(expr.children[0], aliases, locals);
            if (expr.children[1].kind == ExprKind::Slice && expr.children[1].children.size() == 2 &&
                !expr.children[1].children[0].text.empty() &&
                !expr.children[1].children[1].text.empty()) {
                const std::string start = lower_expr(expr.children[1].children[0], aliases, locals);
                const std::string end = lower_expr(expr.children[1].children[1], aliases, locals);
                return "std::span(&(" + out + ")[" + start + "], (" + end + ") - (" + start + "))";
            }
            if (expr.children[1].kind == ExprKind::TupleLiteral) {
                for (const Expr& index : expr.children[1].children) {
                    out += "[" + lower_expr(index, aliases, locals) + "]";
                }
                return out;
            }
            return out + "[" + lower_expr(expr.children[1], aliases, locals) + "]";
        }
        break;
    case ExprKind::ListLiteral:
    case ExprKind::SetLiteral:
        return "{" + join_lowered_exprs(expr.children, aliases, locals) + "}";
    case ExprKind::TupleLiteral:
        return join_lowered_exprs(expr.children, aliases, locals);
    case ExprKind::Lambda:
        return lower_lambda_expr(expr, aliases, locals);
    case ExprKind::Unknown:
        break;
    }
    return lower_expr(expr.text, aliases, locals);
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals) {
    if (expr.kind != ExprKind::ListLiteral) {
        return lower_expr(expr, aliases, locals);
    }
    std::ostringstream out;
    out << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_array_literal(expr.children[i], aliases, locals);
    }
    out << "}";
    return out.str();
}

bool is_build_only_condition(const std::string& text) {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            ++i;
            continue;
        }
        if (!is_identifier_char(c)) {
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < text.size() && is_identifier_char(text[i])) {
            ++i;
        }
        const std::string word = text.substr(start, i - start);
        if (word == "True" || word == "False" || word == "and" || word == "or" || word == "not") {
            continue;
        }
        if (word == "build" && i < text.size() && text[i] == '.') {
            ++i;
            if (i >= text.size() || !is_identifier_char(text[i])) {
                return false;
            }
            while (i < text.size() && is_identifier_char(text[i])) {
                ++i;
            }
            continue;
        }
        return false;
    }
    return text.find("build.") != std::string::npos;
}

bool is_build_value_expr(const Expr& expr);

bool is_build_member_expr(const Expr& expr) {
    return expr.kind == ExprKind::Member && expr.children.size() == 1 &&
           expr.children.front().kind == ExprKind::Name && expr.children.front().name == "build";
}

bool is_build_only_condition(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::StringLiteral:
        return true;
    case ExprKind::Member:
        return is_build_member_expr(expr);
    case ExprKind::Unary:
        return expr.children.size() == 1 && expr.op == "not" &&
               is_build_only_condition(expr.children.front());
    case ExprKind::Binary:
        if (expr.children.size() != 2) {
            return false;
        }
        if (expr.op == "and" || expr.op == "or" || expr.op == "==" || expr.op == "!=" ||
            expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") {
            return is_build_value_expr(expr.children[0]) && is_build_value_expr(expr.children[1]);
        }
        return false;
    case ExprKind::Unknown:
        return is_build_only_condition(expr.text);
    default:
        return false;
    }
}

bool is_build_value_expr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::StringLiteral:
        return true;
    case ExprKind::Member:
        return is_build_member_expr(expr);
    case ExprKind::Unary:
        return expr.children.size() == 1 && expr.op == "not" &&
               is_build_value_expr(expr.children.front());
    case ExprKind::Binary:
        return is_build_only_condition(expr);
    case ExprKind::Unknown:
        return is_build_only_condition(expr.text);
    default:
        return false;
    }
}

std::string if_keyword_for_condition(const Expr& condition) {
    return is_build_only_condition(condition) ? "if constexpr" : "if";
}

std::string unescape_cpp_string(std::string text) {
    std::string out;
    out.reserve(text.size());
    bool escaped = false;
    for (const char c : text) {
        if (!escaped && c == '\\') {
            escaped = true;
            continue;
        }
        if (escaped) {
            out.push_back(c == 'n' ? '\n' : c == 't' ? '\t' : c);
            escaped = false;
            continue;
        }
        out.push_back(c);
    }
    if (escaped) {
        out.push_back('\\');
    }
    return out;
}

std::string cpp_escape_body(std::string text) {
    text = trim_copy(std::move(text));
    if (!starts_with(text, "cpp(") || text.back() != ')') {
        return {};
    }
    text = trim_copy(text.substr(4, text.size() - 5));
    if (starts_with(text, "\"\"\"") && ends_with(text, "\"\"\"")) {
        return text.substr(3, text.size() - 6);
    }
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return unescape_cpp_string(text.substr(1, text.size() - 2));
    }
    return {};
}

std::string cpp_string_literal(std::string text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string lower_declared_stmt_type(const Stmt& stmt, const std::string& effective_type,
                                     const std::vector<std::string>& aliases) {
    return effective_type == stmt.type ? lower_cpp_type(stmt.type_ref, aliases)
                                       : lower_cpp_type(effective_type, aliases);
}

void emit_cpp_escape(std::ostringstream& out, const std::string& text, int depth) {
    std::istringstream body(cpp_escape_body(text));
    std::string line;
    while (std::getline(body, line)) {
        if (!trim_copy(line).empty()) {
            out << indent(depth) << trim_copy(line) << '\n';
        }
    }
}

void emit_source_comment(std::ostringstream& out, const Stmt& stmt, int depth) {
    out << indent(depth) << "// dudu: " << format_location(stmt.location) << '\n';
}

void emit_simple_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                           const std::vector<std::string>& aliases,
                           std::map<std::string, std::string>& locals,
                           const std::string& return_type,
                           const std::map<std::string, std::string>& function_returns) {
    const std::string text = trim_copy(stmt.text);
    if (stmt.kind == StmtKind::CppEscape) {
        emit_cpp_escape(out, text, depth);
        return;
    }
    if (stmt.kind == StmtKind::Pass) {
        out << indent(depth) << "(void)0;\n";
        return;
    }
    if (stmt.kind == StmtKind::Break) {
        out << indent(depth) << "break;\n";
        return;
    }
    if (stmt.kind == StmtKind::Continue) {
        out << indent(depth) << "continue;\n";
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        out << indent(depth) << "throw";
        if (has_expr(stmt.value_expr)) {
            out << ' ' << lower_expr(stmt.value_expr, aliases, locals);
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Delete) {
        out << indent(depth) << "delete " << lower_expr(stmt.value_expr, aliases, locals) << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assert) {
        out << indent(depth) << "if (!(" << lower_expr(stmt.condition_expr, aliases, locals)
            << ")) { throw std::runtime_error(";
        if (has_expr(stmt.message_expr))
            out << lower_expr(stmt.message_expr, aliases, locals);
        else
            out << cpp_string_literal("assert failed: " + stmt.condition_expr.text);
        out << "); }\n";
        return;
    }
    if (stmt.kind == StmtKind::DebugAssert) {
        out << indent(depth) << "assert((" << lower_expr(stmt.condition_expr, aliases, locals)
            << ")";
        if (has_expr(stmt.message_expr))
            out << " && (" << lower_expr(stmt.message_expr, aliases, locals) << ")";
        out << ");\n";
        return;
    }
    if (stmt.kind == StmtKind::Return) {
        out << indent(depth) << "return";
        if (has_expr(stmt.value_expr)) {
            if (starts_with(return_type, "Option[") &&
                stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " std::nullopt";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " {" << lower_expr(stmt.value_expr, aliases, locals) << '}';
            } else {
                out << ' ' << lower_expr(stmt.value_expr, aliases, locals);
            }
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        const std::string& name = stmt.name;
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(stmt.type, stmt.value_expr);
        const std::string type =
            inferred.status == ArrayShapeStatus::Inferred ? inferred.type : stmt.type;
        locals[name] = type;
        out << indent(depth) << lower_declared_stmt_type(stmt, type, aliases) << ' ' << name;
        if (has_expr(stmt.value_expr)) {
            if (starts_with(type, "Option[") && stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " = std::nullopt";
            } else if (starts_with(type, "array[") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {" << lower_array_literal(stmt.value_expr, aliases, locals) << "}";
            } else if (starts_with(type, "list[") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral &&
                       stmt.value_expr.children.empty()) {
                out << " = {}";
            } else if (starts_with(type, "list[") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {" << join_lowered_exprs(stmt.value_expr.children, aliases, locals)
                    << "}";
            } else if (starts_with(type, "dict[") &&
                       stmt.value_expr.kind == ExprKind::DictLiteral) {
                out << " = {" << join_lowered_exprs(stmt.value_expr.children, aliases, locals)
                    << "}";
            } else if (starts_with(type, "set[") && stmt.value_expr.kind == ExprKind::SetLiteral) {
                out << " = {" << join_lowered_exprs(stmt.value_expr.children, aliases, locals)
                    << "}";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " = " << lower_declared_stmt_type(stmt, type, aliases) << "{"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals) << '}';
            } else {
                out << " = " << lower_expr(stmt.value_expr, aliases, locals);
            }
        } else {
            out << "{}";
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
            !names.empty()) {
            out << indent(depth) << "auto [" << join_names(names)
                << "] = " << lower_expr(stmt.value_expr, aliases, locals) << ";\n";
            return;
        }
        if (stmt.target_expr.kind == ExprKind::Name && !stmt.target_expr.name.empty()) {
            const std::string& lhs = stmt.target_expr.name;
            const std::string value = lower_expr(stmt.value_expr, aliases, locals);
            if (locals.contains(lhs)) {
                out << indent(depth) << lhs << " = ";
                if (starts_with(locals.at(lhs), "Option[") &&
                    stmt.value_expr.kind == ExprKind::NoneLiteral) {
                    out << "std::nullopt";
                } else {
                    out << value;
                }
                out << ";\n";
            } else {
                const std::string inferred =
                    infer_emitted_local_type(stmt.value_expr, locals, function_returns);
                locals.emplace(lhs, inferred.empty() ? "auto" : inferred);
                out << indent(depth) << "auto " << lhs << " = " << value << ";\n";
            }
            return;
        }
        if (stmt.target_expr.kind != ExprKind::Unknown) {
            out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals) << " = "
                << lower_expr(stmt.value_expr, aliases, locals) << ";\n";
            return;
        }
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals) << ' ' << stmt.op
            << '=' << " " << lower_expr(stmt.value_expr, aliases, locals) << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Expr) {
        out << indent(depth) << lower_expr(stmt.expr, aliases, locals) << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Unknown) {
        out << indent(depth) << lower_expr(text, aliases, locals) << ";\n";
    }
}

void emit_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                    const std::vector<std::string>& aliases,
                    std::map<std::string, std::string>& locals, const std::string& return_type,
                    const std::map<std::string, std::string>& function_returns) {
    emit_source_comment(out, stmt, depth);
    if (stmt.kind == StmtKind::If) {
        out << indent(depth) << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        out << indent(depth) << "else " << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Else) {
        out << indent(depth) << "else {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Try) {
        out << indent(depth) << "try {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        out << indent(depth);
        if (stmt.name.empty() || stmt.type.empty()) {
            out << "catch (...)";
        } else {
            out << "catch (const " << lower_cpp_type(stmt.type_ref, aliases) << "& " << stmt.name
                << ")";
        }
        out << " {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::While) {
        out << indent(depth) << "while (" << lower_expr(stmt.condition_expr, aliases, locals)
            << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::For && !stmt.iterable.empty()) {
        std::string binding = stmt.name;
        const std::string range = lower_expr(stmt.iterable_expr, aliases, locals);
        std::string binding_type = "auto";
        if (!stmt.type.empty()) {
            binding_type = lower_cpp_type(stmt.type_ref, aliases);
            locals[stmt.name] = stmt.type;
        }
        if (stmt.iterable_expr.kind == ExprKind::Call && stmt.iterable_expr.name == "range") {
            const std::vector<Expr>& args = stmt.iterable_expr.children;
            const std::string start =
                args.size() == 1 ? "0" : lower_expr(args.at(0), aliases, locals);
            const std::string end = args.size() == 1 ? lower_expr(args.at(0), aliases, locals)
                                                     : lower_expr(args.at(1), aliases, locals);
            const std::string step =
                args.size() >= 3 ? lower_expr(args.at(2), aliases, locals) : "1";
            out << indent(depth) << "for (" << binding_type << ' ' << binding << " = " << start
                << "; " << binding << " < " << end << "; " << binding << " += " << step << ") {\n";
            emit_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns);
            out << indent(depth) << "}\n";
            return;
        }
        const std::string loop_type = stmt.type.empty() ? "auto&&" : binding_type;
        out << indent(depth) << "for (" << loop_type << ' ' << binding << " : " << range << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    emit_simple_statement(out, stmt, depth, aliases, locals, return_type, function_returns);
}

} // namespace

std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals) {
    return lower_expr(expr, aliases, locals);
}

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases) {
    emit_block(out, body, depth, aliases, {});
}

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& initial_locals,
                const std::string& return_type,
                const std::map<std::string, std::string>& function_returns) {
    std::map<std::string, std::string> locals = initial_locals;
    for (const Stmt& stmt : body) {
        emit_statement(out, stmt, depth, aliases, locals, return_type, function_returns);
    }
}

} // namespace dudu
