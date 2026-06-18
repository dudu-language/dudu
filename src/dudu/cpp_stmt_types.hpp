#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"

#include <map>
#include <string>

namespace dudu {

TypeRef infer_emitted_local_type_ref(const Expr& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, TypeRef>& local_type_refs,
                                     const std::map<std::string, TypeRef>& function_returns,
                                     const Symbols* symbols = nullptr);

} // namespace dudu
