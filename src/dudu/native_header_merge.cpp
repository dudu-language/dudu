#include "dudu/native_header_merge.hpp"

#include "dudu/ast_type.hpp"

#include <set>

namespace dudu {

std::string native_function_key(const NativeFunctionDecl& fn) {
    std::string key = fn.name + "(";
    for (const std::string& param : native_function_param_type_texts(fn)) {
        key += param + ",";
    }
    return key + (fn.variadic ? "..." : "") + ")->" + native_function_return_type_text(fn) + "/" +
           std::to_string(fn.min_params);
}

void append_unique_native_functions(std::vector<NativeFunctionDecl>& target,
                                    const std::vector<NativeFunctionDecl>& source) {
    std::set<std::string> seen;
    for (const NativeFunctionDecl& item : target) {
        seen.insert(native_function_key(item));
    }
    for (const NativeFunctionDecl& item : source) {
        if (seen.insert(native_function_key(item)).second) {
            target.push_back(item);
        }
    }
}

} // namespace dudu
