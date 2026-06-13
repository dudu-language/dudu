#include "dudu/cpp_stmt_types.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema_scan.hpp"

namespace dudu {

std::string infer_emitted_local_type(const std::string& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, std::string>& function_returns) {
    const std::string text = trim_copy(expr);
    if (const auto local = locals.find(text); local != locals.end()) {
        return local->second;
    }
    const size_t call = find_call_open(text);
    if (call != std::string::npos && find_call_close(text, call) == text.size() - 1) {
        const std::string callee = trim_copy(text.substr(0, call));
        if (const auto fn = function_returns.find(callee); fn != function_returns.end()) {
            return fn->second;
        }
    }
    return {};
}

} // namespace dudu
