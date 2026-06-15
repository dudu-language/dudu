#pragma once

#include "dudu/ast.hpp"
#include "dudu/match_patterns.hpp"
#include "dudu/sema_context.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct EnumCaseBinding {
    size_t field_index = 0;
    std::string name;
};

bool is_wildcard_pattern_expr(const Expr& expr);
bool match_has_guards(const Stmt& stmt);
std::optional<std::string> enum_case_variant_name(const Stmt& stmt);
std::vector<EnumCaseBinding> enum_case_bindings(const Stmt& stmt, const EnumValueDecl& value);
bool match_cases_return(const Stmt& stmt);

} // namespace dudu
