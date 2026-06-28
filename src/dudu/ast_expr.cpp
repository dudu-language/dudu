#include "dudu/ast_expr.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <sstream>

namespace dudu {
namespace {

std::string display_string_literal_value(std::string_view value) {
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"') {
            out += "\\\"";
            continue;
        }
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        if (c == '\r') {
            out += "\\r";
            continue;
        }
        out.push_back(c);
    }
    out += "\"";
    return out;
}

std::string malformed_expr_display(std::string_view kind) {
    return "<malformed " + std::string(kind) + " expression>";
}

} // namespace

std::optional<std::string> path_index_from_expr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::Name:
        return expr.name.empty() ? std::nullopt : std::optional<std::string>{expr.name};
    case ExprKind::IntLiteral:
        return expr.value.empty() ? std::nullopt : std::optional<std::string>{expr.value};
    case ExprKind::StringLiteral:
        return expr.value.empty()
                   ? std::nullopt
                   : std::optional<std::string>{display_string_literal_value(expr.value)};
    case ExprKind::Member:
        if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
            return render_expr_path(*path);
        }
        return std::nullopt;
    case ExprKind::TupleLiteral: {
        std::string out;
        for (const Expr& child : expr.children) {
            const std::optional<std::string> part = path_index_from_expr(child);
            if (!part.has_value()) {
                return std::nullopt;
            }
            if (!out.empty()) {
                out += ", ";
            }
            out += *part;
        }
        return out;
    }
    default:
        return std::nullopt;
    }
}

std::optional<ExprPath> expr_path_from_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        return ExprPath{
            .segments = {
                {.kind = ExprPathSegmentKind::Name, .text = expr.name, .location = expr.location}}};
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 && !expr.name.empty()) {
        std::optional<ExprPath> receiver = expr_path_from_expr(expr.children.front());
        if (receiver.has_value()) {
            receiver->segments.push_back(
                {.kind = ExprPathSegmentKind::Name, .text = expr.name, .location = expr.location});
            return receiver;
        }
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
        std::optional<ExprPath> receiver = expr_path_from_expr(expr.children.front());
        const std::optional<std::string> index = path_index_from_expr(expr.children[1]);
        if (receiver.has_value() && index.has_value()) {
            receiver->segments.push_back({.kind = ExprPathSegmentKind::Index,
                                          .text = *index,
                                          .location = expr.children[1].location});
            return receiver;
        }
    }
    return std::nullopt;
}

std::string render_expr_path(const ExprPath& path) {
    std::string out;
    for (const ExprPathSegment& segment : path.segments) {
        if (segment.kind == ExprPathSegmentKind::Index) {
            out += "[" + segment.text + "]";
            continue;
        }
        if (!out.empty()) {
            out += ".";
        }
        out += segment.text;
    }
    return out;
}

std::string expr_label(const Expr& expr) {
    if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
        return render_expr_path(*path);
    }
    return display_expr(expr);
}

bool expr_missing(const Expr& expr) {
    return expr.kind == ExprKind::Missing;
}

bool expr_present(const Expr& expr) {
    return !expr_missing(expr);
}

std::optional<std::string> bare_callee_name(const Expr& expr) {
    if (!expr.callee.empty() && expr.callee.front().kind == ExprKind::Name &&
        !expr.callee.front().name.empty()) {
        return expr.callee.front().name;
    }
    return std::nullopt;
}

std::optional<std::string> member_callee_name(const Expr& expr) {
    if (expr.kind != ExprKind::Call || expr.callee.size() != 1) {
        return std::nullopt;
    }
    const Expr& callee = expr.callee.front();
    if (callee.kind != ExprKind::Member || callee.name.empty()) {
        return std::nullopt;
    }
    return callee.name;
}

bool is_member_callee(const Expr& expr, std::string_view receiver, std::string_view member) {
    if (expr.kind != ExprKind::Call || expr.callee.size() != 1) {
        return false;
    }
    const Expr& callee = expr.callee.front();
    if (callee.kind != ExprKind::Member || callee.name != member || callee.children.size() != 1) {
        return false;
    }
    const Expr& receiver_expr = callee.children.front();
    return receiver_expr.kind == ExprKind::Name && receiver_expr.name == receiver;
}

std::optional<ExprPath> call_callee_path(const Expr& expr) {
    if (!expr.callee.empty()) {
        return expr_path_from_expr(expr.callee.front());
    }
    return std::nullopt;
}

std::string call_callee_display(const Expr& expr) {
    const std::optional<ExprPath> path = call_callee_path(expr);
    return path ? render_expr_path(*path) : "";
}

std::string direct_callee_name(const Expr& expr) {
    if (expr.kind != ExprKind::Call && expr.kind != ExprKind::TemplateCall) {
        return {};
    }
    return call_callee_display(expr);
}

bool has_expr_template_type_args(const Expr& expr) {
    return expr.template_type_args != nullptr && !expr.template_type_args->empty();
}

const std::vector<TypeRef>& expr_template_type_args(const Expr& expr) {
    static const std::vector<TypeRef> empty;
    return expr.template_type_args == nullptr ? empty : *expr.template_type_args;
}

void set_expr_template_type_args(Expr& expr, std::vector<TypeRef> args) {
    if (args.empty()) {
        expr.template_type_args.reset();
        return;
    }
    expr.template_type_args = std::make_shared<std::vector<TypeRef>>(std::move(args));
}

bool has_expr_template_args(const Expr& expr) {
    return expr.template_args != nullptr && !expr.template_args->empty();
}

const std::vector<Expr>& expr_template_args(const Expr& expr) {
    static const std::vector<Expr> empty;
    return expr.template_args == nullptr ? empty : *expr.template_args;
}

std::vector<Expr>& mutable_expr_template_args(Expr& expr) {
    if (expr.template_args == nullptr) {
        expr.template_args = std::make_shared<std::vector<Expr>>();
    }
    return *expr.template_args;
}

void set_expr_template_args(Expr& expr, std::vector<Expr> args) {
    if (args.empty()) {
        expr.template_args.reset();
        return;
    }
    expr.template_args = std::make_shared<std::vector<Expr>>(std::move(args));
}

bool has_stmt_message_expr(const Stmt& stmt) {
    return stmt.message_expr != nullptr && expr_present(*stmt.message_expr);
}

const Expr& stmt_message_expr(const Stmt& stmt) {
    static const Expr empty;
    return stmt.message_expr == nullptr ? empty : *stmt.message_expr;
}

void set_stmt_message_expr(Stmt& stmt, Expr expr) {
    if (expr_missing(expr)) {
        stmt.message_expr.reset();
        return;
    }
    stmt.message_expr = std::make_shared<Expr>(std::move(expr));
}

bool has_stmt_condition_expr(const Stmt& stmt) {
    return stmt.condition_expr != nullptr && expr_present(*stmt.condition_expr);
}

const Expr& stmt_condition_expr(const Stmt& stmt) {
    static const Expr empty;
    return stmt.condition_expr == nullptr ? empty : *stmt.condition_expr;
}

void set_stmt_condition_expr(Stmt& stmt, Expr expr) {
    if (expr_missing(expr)) {
        stmt.condition_expr.reset();
        return;
    }
    stmt.condition_expr = std::make_shared<Expr>(std::move(expr));
}

bool has_stmt_target_expr(const Stmt& stmt) {
    return stmt.target_expr != nullptr && expr_present(*stmt.target_expr);
}

const Expr& stmt_target_expr(const Stmt& stmt) {
    static const Expr empty;
    return stmt.target_expr == nullptr ? empty : *stmt.target_expr;
}

void set_stmt_target_expr(Stmt& stmt, Expr expr) {
    if (expr_missing(expr)) {
        stmt.target_expr.reset();
        return;
    }
    stmt.target_expr = std::make_shared<Expr>(std::move(expr));
}

bool has_stmt_iterable_expr(const Stmt& stmt) {
    return stmt.iterable_expr != nullptr && expr_present(*stmt.iterable_expr);
}

const Expr& stmt_iterable_expr(const Stmt& stmt) {
    static const Expr empty;
    return stmt.iterable_expr == nullptr ? empty : *stmt.iterable_expr;
}

void set_stmt_iterable_expr(Stmt& stmt, Expr expr) {
    if (expr_missing(expr)) {
        stmt.iterable_expr.reset();
        return;
    }
    stmt.iterable_expr = std::make_shared<Expr>(std::move(expr));
}

bool has_stmt_pattern_expr(const Stmt& stmt) {
    return stmt.pattern_expr != nullptr && expr_present(*stmt.pattern_expr);
}

const Expr& stmt_pattern_expr(const Stmt& stmt) {
    static const Expr empty;
    return stmt.pattern_expr == nullptr ? empty : *stmt.pattern_expr;
}

void set_stmt_pattern_expr(Stmt& stmt, Expr expr) {
    if (expr_missing(expr)) {
        stmt.pattern_expr.reset();
        return;
    }
    stmt.pattern_expr = std::make_shared<Expr>(std::move(expr));
}

bool has_stmt_guard_expr(const Stmt& stmt) {
    return stmt.guard_expr != nullptr && expr_present(*stmt.guard_expr);
}

const Expr& stmt_guard_expr(const Stmt& stmt) {
    static const Expr empty;
    return stmt.guard_expr == nullptr ? empty : *stmt.guard_expr;
}

void set_stmt_guard_expr(Stmt& stmt, Expr expr) {
    if (expr_missing(expr)) {
        stmt.guard_expr.reset();
        return;
    }
    stmt.guard_expr = std::make_shared<Expr>(std::move(expr));
}

std::string join_display_exprs(const std::vector<Expr>& exprs, std::string_view separator) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << display_expr(exprs[i]);
    }
    return out.str();
}

std::string display_template_args(const Expr& expr) {
    std::ostringstream out;
    bool first = true;
    for (const TypeRef& type : expr_template_type_args(expr)) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << substitute_type_ref_text(type, {});
    }
    for (const Expr& value : expr_template_args(expr)) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << display_expr(value);
    }
    return out.str();
}

std::string display_call_expr(const Expr& expr) {
    std::string callee = call_callee_display(expr);
    if (callee.empty() && !expr.callee.empty()) {
        callee = display_expr(expr.callee.front());
    }
    return callee + "(" + join_display_exprs(expr.children, ", ") + ")";
}

std::string display_expr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::Name:
        return expr.name;
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
        return expr.value;
    case ExprKind::StringLiteral:
        return display_string_literal_value(expr.value);
    case ExprKind::NoneLiteral:
        return "None";
    case ExprKind::Unary:
        return expr.children.empty() ? expr.op : expr.op + display_expr(expr.children.front());
    case ExprKind::Binary:
        return expr.children.size() == 2 ? display_expr(expr.children[0]) + " " + expr.op + " " +
                                               display_expr(expr.children[1])
                                         : malformed_expr_display("binary");
    case ExprKind::Call:
        return display_call_expr(expr);
    case ExprKind::TemplateCall:
        return call_callee_display(expr) + "[" + display_template_args(expr) + "](" +
               join_display_exprs(expr.children, ", ") + ")";
    case ExprKind::Member:
        return expr.children.size() == 1 ? display_expr(expr.children.front()) + "." + expr.name
                                         : malformed_expr_display("member");
    case ExprKind::Index:
        return expr.children.size() == 2
                   ? display_expr(expr.children[0]) + "[" + display_expr(expr.children[1]) + "]"
                   : malformed_expr_display("index");
    case ExprKind::ListLiteral:
        return "[" + join_display_exprs(expr.children, ", ") + "]";
    case ExprKind::SetLiteral:
        return "{" + join_display_exprs(expr.children, ", ") + "}";
    case ExprKind::TupleLiteral:
        return "(" + join_display_exprs(expr.children, ", ") + ")";
    case ExprKind::DictEntry:
        return expr.children.size() == 2
                   ? display_expr(expr.children[0]) + ": " + display_expr(expr.children[1])
                   : malformed_expr_display("dict entry");
    case ExprKind::DictLiteral:
        return "{" + join_display_exprs(expr.children, ", ") + "}";
    case ExprKind::NamedArg:
        return expr.children.size() == 1 ? expr.name + "=" + display_expr(expr.children.front())
                                         : malformed_expr_display("named argument");
    case ExprKind::Missing:
        return "";
    case ExprKind::Slice:
        if (expr.children.empty()) {
            return ":";
        }
        if (expr.children.size() == 2) {
            return (expr_missing(expr.children[0]) ? "" : display_expr(expr.children[0])) + ":" +
                   (expr_missing(expr.children[1]) ? "" : display_expr(expr.children[1]));
        }
        return malformed_expr_display("slice");
    case ExprKind::Conditional:
    case ExprKind::DefExpression:
    case ExprKind::Comprehension:
    case ExprKind::Lambda:
    case ExprKind::Await:
    case ExprKind::Yield:
        return "<unsupported " + std::string(expression_kind_name(expr.kind)) + " expression>";
    case ExprKind::CppEscape:
        return "cpp(...)";
    case ExprKind::Unknown:
        return malformed_expr_display("unknown");
    }
    return malformed_expr_display("unknown");
}

} // namespace dudu
