#include "dudu/cpp_match_emit.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_enum.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace dudu {
bool is_wildcard_pattern_expr(const Expr& expr) {
    return expr.kind == ExprKind::Name && expr.name == "_";
}

bool match_has_guards(const Stmt& stmt) {
    for (const Stmt& child : stmt.children) {
        if (child.kind == StmtKind::Case && has_expr(child.guard_expr)) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> enum_case_variant_name(const Stmt& stmt) {
    if (is_wildcard_pattern_expr(stmt.pattern_expr)) {
        return std::string{"_"};
    }
    const Expr* pattern = &stmt.pattern_expr;
    if (stmt.pattern_expr.kind == ExprKind::Call && !stmt.pattern_expr.callee.empty()) {
        pattern = &stmt.pattern_expr.callee.front();
    }
    const std::optional<std::string> path = member_path_from_expr(*pattern);
    if (!path) {
        return std::nullopt;
    }
    const size_t dot = path->find('.');
    if (dot == std::string::npos || path->find('.', dot + 1) != std::string::npos) {
        return std::nullopt;
    }
    return path->substr(dot + 1);
}

std::vector<EnumCaseBinding> enum_case_bindings(const Stmt& stmt, const EnumValueDecl& value) {
    std::vector<EnumCaseBinding> out;
    if (stmt.pattern_expr.kind != ExprKind::Call) {
        return out;
    }
    for (size_t i = 0; i < stmt.pattern_expr.children.size(); ++i) {
        const Expr& child = stmt.pattern_expr.children[i];
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

WrapperMatchType wrapper_match_type(const std::string& type) {
    const std::string trimmed = trim_copy(type);
    if (starts_with(trimmed, "Option[") && trimmed.back() == ']') {
        return {.kind = WrapperMatchKind::Option,
                .args = split_top_level_args(trimmed.substr(7, trimmed.size() - 8))};
    }
    if (starts_with(trimmed, "Result[") && trimmed.back() == ']') {
        return {.kind = WrapperMatchKind::Result,
                .args = split_top_level_args(trimmed.substr(7, trimmed.size() - 8))};
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
    if (pattern.kind == ExprKind::Call && !pattern.callee.empty() &&
        pattern.callee.front().kind == ExprKind::Name) {
        return pattern.callee.front().name;
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

bool match_cases_return(const Stmt& stmt) {
    if (stmt.children.empty()) {
        return false;
    }
    for (const Stmt& child : stmt.children) {
        if (child.kind != StmtKind::Case || !block_guarantees_return(child.children)) {
            return false;
        }
    }
    return true;
}

} // namespace dudu
