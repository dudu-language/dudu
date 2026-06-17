#pragma once

#include "dudu/ast.hpp"

#include <map>
#include <string>

namespace dudu {

std::string infer_emitted_local_type(const Expr& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, TypeRef>& function_returns);
std::string infer_emitted_local_type(const Expr& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, TypeRef>& local_type_refs,
                                     const std::map<std::string, TypeRef>& function_returns);

} // namespace dudu
