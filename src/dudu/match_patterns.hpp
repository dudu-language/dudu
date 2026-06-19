#pragma once

#include "dudu/ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct EnumCaseBinding {
    size_t field_index = 0;
    std::string name;
};

enum class WrapperMatchKind {
    None,
    Option,
    Result,
};

struct WrapperMatchType {
    WrapperMatchKind kind = WrapperMatchKind::None;
    std::vector<TypeRef> arg_refs;
};

bool is_wildcard_pattern_expr(const Expr& expr);
std::optional<std::string> enum_case_variant_name(const Stmt& stmt);
std::optional<std::string> enum_case_variant_name_for(const EnumDecl& en, const Stmt& stmt);
std::vector<EnumCaseBinding> enum_case_bindings(const Stmt& stmt, const EnumValueDecl& value);
WrapperMatchType wrapper_match_type(const TypeRef& type);
std::optional<std::string> wrapper_case_name(const Expr& pattern);
std::optional<std::string> wrapper_case_binding_name(const Expr& pattern);

} // namespace dudu
