#pragma once

#include "dudu/ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

enum class WrapperMatchKind {
    None,
    Option,
    Result,
};

struct WrapperMatchType {
    WrapperMatchKind kind = WrapperMatchKind::None;
    std::vector<std::string> args;
};

WrapperMatchType wrapper_match_type(const std::string& type);
std::optional<std::string> wrapper_case_name(const Expr& pattern);
std::optional<std::string> wrapper_case_binding_name(const Expr& pattern);

} // namespace dudu
