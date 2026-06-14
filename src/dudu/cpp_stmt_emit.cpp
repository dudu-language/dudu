#include "dudu/cpp_stmt_emit.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_pointer_members.hpp"
#include "dudu/cpp_stmt_types.hpp"

#include <map>
#include <sstream>

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
        if (arg.text.find('=') != std::string::npos) {
            return true;
        }
    }
    return false;
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
    case ExprKind::StringLiteral:
    case ExprKind::Name:
    case ExprKind::CppEscape:
        return lower_expr(expr.text, aliases, locals);
    case ExprKind::Unary:
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
        if (has_named_argument_shape(expr.children)) {
            break;
        }
        return lower_expr(expr.name + "(" + join_lowered_exprs(expr.children, aliases, locals) +
                              ")",
                          aliases, locals);
    case ExprKind::Member:
        if (expr.children.size() == 1) {
            return lower_expr(lower_expr(expr.children.front(), aliases, locals) + "." + expr.name,
                              aliases, locals);
        }
        break;
    case ExprKind::Index:
        break;
    case ExprKind::ListLiteral:
    case ExprKind::TupleLiteral:
    case ExprKind::SetLiteral:
    case ExprKind::DictLiteral:
    case ExprKind::Lambda:
    case ExprKind::TemplateCall:
        break;
    case ExprKind::Unknown:
        break;
    }
    return lower_expr(expr.text, aliases, locals);
}

size_t find_top_level_colon(const std::string& text) {
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
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == ':' && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_tuple_literal(const std::string& value) {
    if (split_top_level_args(value).size() > 1) {
        return true;
    }
    return starts_with(value, "(") && ends_with(value, ")") &&
           split_top_level_args(value.substr(1, value.size() - 2)).size() > 1;
}

std::string tuple_literal_body(const std::string& value) {
    return starts_with(value, "(") && ends_with(value, ")") ? value.substr(1, value.size() - 2)
                                                            : value;
}

std::string lower_literal_value(const std::string& value, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals);

std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals) {
    if (expr.kind != ExprKind::ListLiteral) {
        return lower_expr(expr.text, aliases, locals);
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

std::string lower_dict_literal_body(const std::string& value,
                                    const std::vector<std::string>& aliases,
                                    const std::map<std::string, std::string>& locals) {
    const std::string body = value.substr(1, value.size() - 2);
    std::ostringstream out;
    const std::vector<std::string> entries = split_top_level_args(body);
    for (size_t i = 0; i < entries.size(); ++i) {
        const size_t colon = find_top_level_colon(entries[i]);
        if (colon == std::string::npos) {
            continue;
        }
        if (i > 0) {
            out << ", ";
        }
        out << "{" << lower_expr(entries[i].substr(0, colon), aliases, locals) << ", ";
        out << lower_literal_value(entries[i].substr(colon + 1), aliases, locals) << "}";
    }
    return out.str();
}

bool is_dict_literal_value(std::string value) {
    value = trim_copy(std::move(value));
    if (!starts_with(value, "{") || !ends_with(value, "}")) {
        return false;
    }
    for (const std::string& entry : split_top_level_args(value.substr(1, value.size() - 2))) {
        if (find_top_level_colon(entry) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string lower_literal_value(const std::string& value, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals) {
    const std::string trimmed = trim_copy(value);
    if (is_dict_literal_value(trimmed)) {
        return "{" + lower_dict_literal_body(trimmed, aliases, locals) + "}";
    }
    return lower_expr(trimmed, aliases, locals);
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

std::string if_keyword_for_condition(const std::string& condition) {
    return is_build_only_condition(condition) ? "if constexpr" : "if";
}

std::string normalize_spaced_compound(std::string text) {
    for (const std::pair<std::string_view, std::string_view> op :
         {std::pair{" + =", " +="}, std::pair{" - =", " -="}, std::pair{" * =", " *="},
          std::pair{" / =", " /="}, std::pair{" % =", " %="}, std::pair{" ^ =", " ^="},
          std::pair{" & =", " &="}, std::pair{" | =", " |="}}) {
        size_t pos = text.find(op.first);
        while (pos != std::string::npos) {
            text.replace(pos, op.first.size(), op.second);
            pos = text.find(op.first, pos + op.second.size());
        }
    }
    return text;
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
    if (stmt.kind == StmtKind::Raise) {
        const std::string value = stmt.value;
        out << indent(depth) << "throw";
        if (!value.empty())
            out << ' ' << lower_expr(stmt.value_expr, aliases, locals);
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
        if (!stmt.message.empty())
            out << lower_expr(stmt.message_expr, aliases, locals);
        else
            out << cpp_string_literal("assert failed: " + stmt.condition);
        out << "); }\n";
        return;
    }
    if (stmt.kind == StmtKind::DebugAssert) {
        out << indent(depth) << "assert((" << lower_expr(stmt.condition_expr, aliases, locals)
            << ")";
        if (!stmt.message.empty())
            out << " && (" << lower_expr(stmt.message_expr, aliases, locals) << ")";
        out << ");\n";
        return;
    }
    if (stmt.kind == StmtKind::Return) {
        const std::string value = stmt.value;
        out << indent(depth) << "return";
        if (!value.empty()) {
            if (starts_with(return_type, "Option[") && value == "None") {
                out << " std::nullopt";
            } else if (split_top_level_args(value).size() > 1) {
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
        if (!stmt.value.empty()) {
            const std::string& value = stmt.value;
            if (starts_with(type, "Option[") && value == "None") {
                out << " = std::nullopt";
            } else if (starts_with(type, "array[") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {" << lower_array_literal(stmt.value_expr, aliases, locals) << "}";
            } else if (starts_with(type, "list[") && value == "[]") {
                out << " = {}";
            } else if (starts_with(type, "list[") && starts_with(value, "[") &&
                       ends_with(value, "]")) {
                out << " = {" << lower_expr(value.substr(1, value.size() - 2), aliases, locals)
                    << '}';
            } else if (starts_with(type, "dict[") && starts_with(value, "{") &&
                       ends_with(value, "}")) {
                out << " = {" << lower_dict_literal_body(value, aliases, locals) << '}';
            } else if (starts_with(type, "set[") && starts_with(value, "{") &&
                       ends_with(value, "}")) {
                out << " = {" << lower_expr(value.substr(1, value.size() - 2), aliases, locals)
                    << '}';
            } else if (is_tuple_literal(value)) {
                out << " = " << lower_declared_stmt_type(stmt, type, aliases) << "{"
                    << lower_expr(tuple_literal_body(value), aliases, locals) << '}';
            } else {
                out << " = " << lower_expr(value, aliases, locals);
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
            const std::string& raw_value = stmt.value;
            const std::string value = lower_expr(stmt.value_expr, aliases, locals);
            if (locals.contains(lhs)) {
                out << indent(depth) << lhs << " = ";
                if (starts_with(locals.at(lhs), "Option[") && raw_value == "None") {
                    out << "std::nullopt";
                } else {
                    out << value;
                }
                out << ";\n";
            } else {
                const std::string inferred =
                    infer_emitted_local_type(raw_value, locals, function_returns);
                locals.emplace(lhs, inferred.empty() ? "auto" : inferred);
                out << indent(depth) << "auto " << lhs << " = " << value << ";\n";
            }
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
    out << indent(depth) << lower_expr(normalize_spaced_compound(text), aliases, locals) << ";\n";
}

void emit_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                    const std::vector<std::string>& aliases,
                    std::map<std::string, std::string>& locals, const std::string& return_type,
                    const std::map<std::string, std::string>& function_returns) {
    emit_source_comment(out, stmt, depth);
    if (stmt.kind == StmtKind::If) {
        const std::string& condition = stmt.condition;
        out << indent(depth) << if_keyword_for_condition(condition) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        const std::string& condition = stmt.condition;
        out << indent(depth) << "else " << if_keyword_for_condition(condition) << " ("
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
            binding_type = lower_cpp_type(stmt.type, aliases);
            locals[stmt.name] = stmt.type;
        }
        if (starts_with(range, "range(") && ends_with(range, ")")) {
            const std::vector<std::string> args =
                split_top_level_args(range.substr(6, range.size() - 7));
            const std::string start = args.size() == 1 ? "0" : args.at(0);
            const std::string end = args.size() == 1 ? args.at(0) : args.at(1);
            const std::string step = args.size() >= 3 ? args.at(2) : "1";
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
