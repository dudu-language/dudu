#include "dudu/native_header_merge.hpp"

#include "dudu/ast_type.hpp"

#include <algorithm>

namespace dudu {

bool type_ref_lists_equivalent(const std::vector<TypeRef>& lhs, const std::vector<TypeRef>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!type_ref_equivalent(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool native_function_equivalent(const NativeFunctionDecl& lhs, const NativeFunctionDecl& rhs) {
    return lhs.name == rhs.name && lhs.variadic == rhs.variadic &&
           lhs.min_params == rhs.min_params &&
           type_ref_equivalent(native_function_return_type_ref(lhs),
                               native_function_return_type_ref(rhs)) &&
           type_ref_lists_equivalent(native_function_param_type_refs(lhs),
                                     native_function_param_type_refs(rhs));
}

bool contains_equivalent_native_function(const std::vector<NativeFunctionDecl>& functions,
                                         const NativeFunctionDecl& candidate) {
    return std::ranges::any_of(functions, [&](const NativeFunctionDecl& fn) {
        return native_function_equivalent(fn, candidate);
    });
}

void append_unique_native_functions(std::vector<NativeFunctionDecl>& target,
                                    const std::vector<NativeFunctionDecl>& source) {
    for (const NativeFunctionDecl& item : source) {
        if (!contains_equivalent_native_function(target, item)) {
            target.push_back(item);
        }
    }
}

} // namespace dudu
