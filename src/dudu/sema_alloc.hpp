#pragma once

#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::optional<std::string> infer_allocation_call(const Symbols& symbols,
                                                 const SourceLocation* location,
                                                 const std::string& callee,
                                                 const std::vector<Expr>& args);
std::optional<std::string> infer_allocation_call(const Symbols& symbols,
                                                 const SourceLocation* location,
                                                 const std::string& callee,
                                                 const std::vector<TypeRef>& type_args,
                                                 size_t arg_count);
bool is_deallocation_call(std::string_view callee);
void check_deallocation_args(const SourceLocation& location, std::string_view callee,
                             const std::vector<std::string>& arg_types);

} // namespace dudu
