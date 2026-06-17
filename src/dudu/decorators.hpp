#pragma once

#include "dudu/ast.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace dudu {

std::string decorator_name(const Decorator& decorator);
bool decorator_matches(const Decorator& decorator, std::string_view name);
bool has_decorator(const std::vector<Decorator>& decorators, std::string_view name);
bool decorator_call_matches(const Decorator& decorator, std::string_view name);
std::optional<std::string> decorator_first_arg_text(const Decorator& decorator,
                                                    std::string_view name);
std::optional<std::string> decorator_arg_list_text(const Decorator& decorator,
                                                   std::string_view name);
std::optional<std::string> decorator_first_string_arg(const Decorator& decorator,
                                                      std::string_view name);
std::optional<std::string> decorator_first_string_literal_arg(const Decorator& decorator,
                                                              std::string_view name);
bool decorator_has_single_string_literal_arg(const Decorator& decorator, std::string_view name);

} // namespace dudu
