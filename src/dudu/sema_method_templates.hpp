#pragma once

#include "dudu/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

std::vector<std::string> template_args_from_type(const TypeRef& type);
std::vector<TypeRef> template_arg_refs_from_type(const TypeRef& type);
TypeRef substitute_receiver_template_type(const TypeRef& type,
                                          const std::vector<std::string>& receiver_args);
TypeRef substitute_receiver_template_type(const TypeRef& type,
                                          const std::vector<TypeRef>& receiver_args);

} // namespace dudu
