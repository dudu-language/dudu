#include "dudu/match_patterns.hpp"

#include "dudu/cpp_lower.hpp"

namespace dudu {

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

} // namespace dudu
