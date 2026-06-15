#include "dudu/native_header_merge.hpp"

#include <set>

namespace dudu {

std::string native_function_key(const NativeFunctionDecl& fn) {
    std::string key = fn.name + "(";
    for (const std::string& param : fn.params) {
        key += param + ",";
    }
    return key + (fn.variadic ? "..." : "") + ")->" + fn.return_type + "/" +
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
