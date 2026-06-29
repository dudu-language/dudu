#include "dudu/core/match_patterns.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"

#include <algorithm>

namespace dudu {

namespace {

const Expr* enum_pattern_head(const Stmt& stmt) {
    if (stmt_pattern_expr(stmt).kind == ExprKind::Call &&
        has_expr_callee(stmt_pattern_expr(stmt))) {
        return &expr_callee(stmt_pattern_expr(stmt)).front();
    }
    return &stmt_pattern_expr(stmt);
}

std::optional<std::pair<std::string, std::string>> enum_variant_path_expr(const Expr& expr) {
    if (expr.kind != ExprKind::Member || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Name) {
        return std::nullopt;
    }
    return std::make_pair(expr.children.front().name, expr.name);
}

} // namespace

bool is_wildcard_pattern_expr(const Expr& expr) {
    return expr.kind == ExprKind::Name && expr.name == "_";
}

std::optional<std::string> enum_case_variant_name(const Stmt& stmt) {
    if (is_wildcard_pattern_expr(stmt_pattern_expr(stmt))) {
        return std::string{"_"};
    }
    const auto path = enum_variant_path_expr(*enum_pattern_head(stmt));
    if (!path) {
        return std::nullopt;
    }
    return path->second;
}

std::optional<std::string> enum_case_variant_name_for(const EnumDecl& en, const Stmt& stmt) {
    if (is_wildcard_pattern_expr(stmt_pattern_expr(stmt))) {
        return std::string{"_"};
    }
    const auto path = enum_variant_path_expr(*enum_pattern_head(stmt));
    if (!path || path->first != en.name) {
        return std::nullopt;
    }
    return path->second;
}

std::vector<EnumCaseBinding> enum_case_bindings(const Stmt& stmt, const EnumValueDecl& value) {
    std::vector<EnumCaseBinding> out;
    if (stmt_pattern_expr(stmt).kind != ExprKind::Call) {
        return out;
    }
    for (size_t i = 0; i < stmt_pattern_expr(stmt).children.size(); ++i) {
        const Expr& child = stmt_pattern_expr(stmt).children[i];
        if (child.kind == ExprKind::Name && !child.name.empty()) {
            out.push_back({.field_index = i, .name = child.name});
        } else if (child.kind == ExprKind::NamedArg && child.children.size() == 1 &&
                   child.children.front().kind == ExprKind::Name &&
                   !child.children.front().name.empty()) {
            const auto found = std::find_if(
                value.payload_fields.begin(), value.payload_fields.end(),
                [&](const EnumPayloadField& field) { return field.name == child.name; });
            if (found != value.payload_fields.end()) {
                out.push_back({.field_index = static_cast<size_t>(
                                   std::distance(value.payload_fields.begin(), found)),
                               .name = child.children.front().name});
            }
        }
    }
    return out;
}

WrapperMatchType wrapper_match_type(const TypeRef& type) {
    if (const std::vector<TypeRef> args = template_type_arg_refs(type, "Option");
        args.size() == 1) {
        return {.kind = WrapperMatchKind::Option, .arg_refs = args};
    }
    if (const std::vector<TypeRef> args = template_type_arg_refs(type, "Result"); !args.empty()) {
        return {.kind = WrapperMatchKind::Result, .arg_refs = args};
    }
    return {};
}

std::optional<std::string> wrapper_case_name(const Expr& pattern) {
    if (pattern.kind == ExprKind::Name && pattern.name == "_") {
        return std::string{"_"};
    }
    if (pattern.kind == ExprKind::NoneLiteral) {
        return std::string{"None"};
    }
    if (pattern.kind == ExprKind::Call && has_expr_callee(pattern) &&
        expr_callee(pattern).front().kind == ExprKind::Name) {
        return expr_callee(pattern).front().name;
    }
    return std::nullopt;
}

std::optional<std::string> wrapper_case_binding_name(const Expr& pattern) {
    if (pattern.kind == ExprKind::Call && pattern.children.size() == 1 &&
        pattern.children.front().kind == ExprKind::Name && !pattern.children.front().name.empty()) {
        return pattern.children.front().name;
    }
    return std::nullopt;
}

} // namespace dudu
