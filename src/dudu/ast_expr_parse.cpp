#include "dudu/ast_parse_utils.hpp"
#include "dudu/cpp_lower.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace dudu {
namespace {
std::vector<Expr> parse_expr_list(std::string_view text, SourceLocation location) {
    std::vector<Expr> out;
    for (const CommaPart& part : split_top_level_comma_parts(text)) {
        if (!part.text.empty()) {
            out.push_back(parse_expr_text(part.text, advance_columns(location, part.offset)));
        }
    }
    return out;
}

std::vector<Expr> parse_dict_entries(std::string_view text, SourceLocation location) {
    std::vector<Expr> out;
    for (const CommaPart& part : split_top_level_comma_parts(text)) {
        if (part.text.empty()) {
            continue;
        }
        Expr entry =
            make_expr(ExprKind::DictEntry, part.text, advance_columns(location, part.offset));
        const size_t colon = find_top_level_colon(part.text);
        if (colon == std::string_view::npos) {
            entry.children.push_back(parse_expr_text(part.text, entry.location));
        } else {
            entry.children.push_back(parse_expr_text(part.text.substr(0, colon), entry.location));
            entry.children.push_back(parse_expr_text(part.text.substr(colon + 1),
                                                     advance_columns(entry.location, colon + 1)));
        }
        out.push_back(std::move(entry));
    }
    return out;
}

bool is_builtin_pointer_cast_type(std::string_view type) {
    type = trim_view(type);
    return type == "bool" || type == "i8" || type == "i16" || type == "i32" || type == "i64" ||
           type == "u8" || type == "u16" || type == "u32" || type == "u64" || type == "isize" ||
           type == "usize" || type == "f32" || type == "f64" || type == "void" || type == "cstr";
}

bool is_pointer_cast_call(std::string_view text) {
    if (!text.starts_with("*") || !text.ends_with(")")) {
        return false;
    }
    const size_t open = find_matching_open(text, text.size() - 1, '(', ')');
    if (open == std::string_view::npos || open <= 1) {
        return false;
    }
    std::string_view type = trim_view(text.substr(1, open - 1));
    if (type.empty() || type.front() == '(') {
        return false;
    }
    if (type.starts_with("struct ") || type.starts_with("const[") ||
        type.starts_with("volatile[") || type.starts_with("atomic[")) {
        return true;
    }
    if (type.ends_with("]") && type.find('[') != std::string_view::npos) {
        return true;
    }
    return is_builtin_pointer_cast_type(type) ||
           std::isupper(static_cast<unsigned char>(type.front())) != 0;
}

} // namespace

Expr parse_expr_text(std::string_view text, SourceLocation location) {
    text = trim_view_with_location(text, location);
    if (text.empty()) {
        return make_expr(ExprKind::Unknown, text, location);
    }
    if (enclosed_by_outer_pair(text, '(', ')')) {
        const std::string_view inner = text.substr(1, text.size() - 2);
        if (split_top_level_commas(trim_view(inner)).size() > 1) {
            Expr expr = make_expr(ExprKind::TupleLiteral, text, location);
            expr.children = parse_expr_list(inner, advance_columns(location, 1));
            return expr;
        }
        return parse_expr_text(inner, advance_columns(location, 1));
    }
    if (starts_keyword(text, "lambda")) {
        Expr expr = make_expr(ExprKind::Lambda, text, location);
        size_t args_start = 6;
        while (args_start < text.size() &&
               std::isspace(static_cast<unsigned char>(text[args_start])) != 0) {
            ++args_start;
        }
        const std::string_view body = text.substr(args_start);
        const size_t colon = find_top_level_colon(body);
        if (colon != std::string_view::npos) {
            const std::string_view args = body.substr(0, colon);
            expr.name = trim_string(args);
            expr.params = parse_expr_list(args, advance_columns(location, args_start));
            expr.children.push_back(parse_expr_text(
                body.substr(colon + 1), advance_columns(location, args_start + colon + 1)));
        }
        return expr;
    }
    if (split_top_level_commas(text).size() > 1) {
        Expr expr = make_expr(ExprKind::TupleLiteral, text, location);
        expr.children = parse_expr_list(text, location);
        return expr;
    }
    if (enclosed_by_outer_pair(text, '[', ']')) {
        Expr expr = make_expr(ExprKind::ListLiteral, text, location);
        expr.children =
            parse_expr_list(text.substr(1, text.size() - 2), advance_columns(location, 1));
        return expr;
    }
    if (enclosed_by_outer_pair(text, '{', '}')) {
        const std::string_view body = text.substr(1, text.size() - 2);
        Expr expr =
            make_expr(has_top_level_colon(body) ? ExprKind::DictLiteral : ExprKind::SetLiteral,
                      text, location);
        expr.children = expr.kind == ExprKind::DictLiteral
                            ? parse_dict_entries(body, advance_columns(location, 1))
                            : parse_expr_list(body, advance_columns(location, 1));
        return expr;
    }
    if ((text.front() == '"' && text.back() == '"') ||
        (text.front() == '\'' && text.back() == '\'')) {
        Expr expr = make_expr(ExprKind::StringLiteral, text, location);
        expr.value = std::string(text.substr(1, text.size() - 2));
        return expr;
    }
    if (text == "True" || text == "False") {
        Expr expr = make_expr(ExprKind::BoolLiteral, text, location);
        expr.value = std::string(text);
        return expr;
    }
    if (text == "None") {
        return make_expr(ExprKind::NoneLiteral, text, location);
    }
    const size_t slice_colon = find_top_level_colon(text);
    if (slice_colon != std::string_view::npos) {
        Expr expr = make_expr(ExprKind::Slice, text, location);
        expr.children.push_back(parse_expr_text(text.substr(0, slice_colon), location));
        expr.children.push_back(parse_expr_text(text.substr(slice_colon + 1),
                                                advance_columns(location, slice_colon + 1)));
        return expr;
    }

    const size_t named_arg_assign = find_top_level_assignment(text, false);
    if (named_arg_assign != std::string_view::npos) {
        const std::string_view name = trim_view(text.substr(0, named_arg_assign));
        if (is_identifier(name)) {
            Expr expr = make_expr(ExprKind::NamedArg, text, location);
            expr.name = std::string(name);
            expr.children.push_back(
                parse_expr_text(text.substr(named_arg_assign + 1),
                                advance_columns(location, named_arg_assign + 1)));
            return expr;
        }
    }

    const size_t conditional_if = find_top_level_word(text, "if");
    if (conditional_if != std::string_view::npos) {
        const size_t conditional_else =
            find_top_level_word(text.substr(conditional_if + 2), "else");
        if (conditional_else != std::string_view::npos) {
            Expr expr = make_expr(ExprKind::Conditional, text, location);
            const size_t else_pos = conditional_if + 2 + conditional_else;
            expr.children.push_back(parse_expr_text(text.substr(0, conditional_if), location));
            expr.children.push_back(
                parse_expr_text(text.substr(conditional_if + 2, else_pos - conditional_if - 2),
                                advance_columns(location, conditional_if + 2)));
            expr.children.push_back(parse_expr_text(text.substr(else_pos + 4),
                                                    advance_columns(location, else_pos + 4)));
            return expr;
        }
    }

    const std::vector<std::vector<std::string_view>> precedence = {
        {"or"},       {"and"},    {"==", "!=", "<=", ">=", "<", ">"},
        {"|"},        {"^"},      {"&"},
        {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"},
    };
    for (const std::vector<std::string_view>& ops : precedence) {
        const size_t op_pos = find_top_level_binary_operator(text, ops);
        if (op_pos != std::string_view::npos) {
            std::string_view op;
            for (const std::string_view candidate : ops) {
                if (text.substr(op_pos, candidate.size()) == candidate) {
                    op = candidate;
                    break;
                }
            }
            Expr expr = make_expr(ExprKind::Binary, text, location);
            expr.op = std::string(op);
            expr.children.push_back(parse_expr_text(text.substr(0, op_pos), location));
            expr.children.push_back(parse_expr_text(text.substr(op_pos + op.size()),
                                                    advance_columns(location, op_pos + op.size())));
            return expr;
        }
    }

    if (starts_keyword(text, "not")) {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "not";
        expr.children.push_back(parse_expr_text(text.substr(3), advance_columns(location, 3)));
        return expr;
    }
    if (starts_keyword(text, "await")) {
        Expr expr = make_expr(ExprKind::Await, text, location);
        expr.children.push_back(parse_expr_text(text.substr(5), advance_columns(location, 5)));
        return expr;
    }
    if (starts_keyword(text, "yield")) {
        Expr expr = make_expr(ExprKind::Yield, text, location);
        expr.children.push_back(parse_expr_text(text.substr(5), advance_columns(location, 5)));
        return expr;
    }
    if (text.front() == '*' && !is_pointer_cast_call(text)) {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "*";
        expr.children.push_back(parse_expr_text(text.substr(1), advance_columns(location, 1)));
        return expr;
    }
    if (text.front() == '-' && !is_integer_literal(text) && !is_float_literal(text)) {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "-";
        expr.children.push_back(parse_expr_text(text.substr(1), advance_columns(location, 1)));
        return expr;
    }
    if (text.front() == '&') {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "&";
        expr.children.push_back(parse_expr_text(text.substr(1), advance_columns(location, 1)));
        return expr;
    }
    if (text.front() == '~') {
        Expr expr = make_expr(ExprKind::Unary, text, location);
        expr.op = "~";
        expr.children.push_back(parse_expr_text(text.substr(1), advance_columns(location, 1)));
        return expr;
    }

    if (text.starts_with("cpp(") && text.ends_with(")")) {
        Expr expr = make_expr(ExprKind::CppEscape, text, location);
        expr.value = cpp_escape_body(std::string(text));
        return expr;
    }

    if (text.ends_with(")")) {
        const size_t open = find_matching_open(text, text.size() - 1, '(', ')');
        if (open != std::string_view::npos && open > 0) {
            std::string_view callee = trim_view(text.substr(0, open));
            Expr expr = make_expr(ExprKind::Call, text, location);
            expr.name = trim_string(callee);
            expr.callee.push_back(parse_expr_text(callee, location));
            expr.children = parse_expr_list(text.substr(open + 1, text.size() - open - 2),
                                            advance_columns(location, open + 1));
            if (callee.ends_with("]")) {
                const size_t type_open = find_matching_open(callee, callee.size() - 1, '[', ']');
                if (type_open != std::string_view::npos && type_open > 0) {
                    expr.kind = ExprKind::TemplateCall;
                    expr.name = trim_string(callee.substr(0, type_open));
                    expr.callee.clear();
                    expr.callee.push_back(parse_expr_text(callee.substr(0, type_open), location));
                    expr.template_args =
                        parse_expr_list(callee.substr(type_open + 1, callee.size() - type_open - 2),
                                        advance_columns(location, type_open + 1));
                    expr.template_type_args =
                        parse_type_list(callee.substr(type_open + 1, callee.size() - type_open - 2),
                                        advance_columns(location, type_open + 1));
                }
            }
            return expr;
        }
    }
    if (text.ends_with("]")) {
        const size_t open = find_matching_open(text, text.size() - 1, '[', ']');
        if (open != std::string_view::npos && open > 0) {
            Expr expr = make_expr(ExprKind::Index, text, location);
            expr.children.push_back(parse_expr_text(text.substr(0, open), location));
            expr.children.push_back(parse_expr_text(text.substr(open + 1, text.size() - open - 2),
                                                    advance_columns(location, open + 1)));
            return expr;
        }
    }
    const size_t dot = find_top_level_member_dot(text);
    if (dot != std::string_view::npos) {
        Expr expr = make_expr(ExprKind::Member, text, location);
        expr.name = trim_string(text.substr(dot + 1));
        expr.children.push_back(parse_expr_text(text.substr(0, dot), location));
        return expr;
    }
    if (is_float_literal(text)) {
        Expr expr = make_expr(ExprKind::FloatLiteral, text, location);
        expr.value = std::string(text);
        return expr;
    }
    if (is_integer_literal(text)) {
        Expr expr = make_expr(ExprKind::IntLiteral, text, location);
        expr.value = std::string(text);
        return expr;
    }
    if (is_identifier(text)) {
        Expr expr = make_expr(ExprKind::Name, text, location);
        expr.name = std::string(text);
        return expr;
    }
    return make_expr(ExprKind::Unknown, text, location);
}

} // namespace dudu
