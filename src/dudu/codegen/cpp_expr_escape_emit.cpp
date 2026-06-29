#include "dudu/codegen/cpp_expr_emit.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_pointer_members.hpp"

#include <utility>

namespace dudu {

std::string lower_cpp_escape_expr(std::string expr, const std::vector<std::string>& aliases,
                                  const std::map<std::string, TypeRef>& local_type_refs) {
    return lower_raw_cpp_escape_expr(rewrite_pointer_members(std::move(expr), local_type_refs),
                                     aliases);
}

} // namespace dudu
