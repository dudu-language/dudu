#pragma once

#include "dudu/core/ast.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace dudu {

std::string decorator_name(const Decorator& decorator);
bool decorator_matches(const Decorator& decorator, std::string_view name);
bool has_decorator(const std::vector<Decorator>& decorators, std::string_view name);
bool has_decorator(const FunctionDecl& function, std::string_view name);
bool has_decorator(const ClassDecl& klass, std::string_view name);
bool is_test_function(const FunctionDecl& function);
bool decorator_call_matches(const Decorator& decorator, std::string_view name);
std::optional<std::string> decorator_first_arg_display(const Decorator& decorator,
                                                       std::string_view name);
std::optional<std::string> decorator_arg_list_display(const Decorator& decorator,
                                                      std::string_view name);
std::optional<std::string> decorator_first_string_literal_arg(const Decorator& decorator,
                                                              std::string_view name);
bool decorator_has_single_string_literal_arg(const Decorator& decorator, std::string_view name);

} // namespace dudu
