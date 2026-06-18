#pragma once

#include "dudu/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

std::vector<std::string> template_args_from_type(const TypeRef& type);
std::vector<std::string> template_args_from_type(const std::string& type);
TypeRef substitute_receiver_template_type(const TypeRef& type,
                                          const std::vector<std::string>& receiver_args);
std::string substitute_receiver_template_type(std::string type,
                                              const std::vector<std::string>& receiver_args);

} // namespace dudu
