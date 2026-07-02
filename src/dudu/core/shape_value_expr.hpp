#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace dudu {

bool shape_value_expr_valid(std::string_view text);
std::string normalize_shape_value_expr(std::string_view text);
std::set<std::string> shape_value_expr_identifiers(std::string_view text);
std::optional<long long>
shape_value_expr_eval(std::string_view text, const std::map<std::string, long long>& bindings = {});
std::string shape_value_expr_substitute(std::string_view text,
                                        const std::map<std::string, std::string>& bindings);
bool shape_value_expr_equivalent(std::string_view left, std::string_view right);

} // namespace dudu
