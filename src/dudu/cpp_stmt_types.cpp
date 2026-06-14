#include "dudu/cpp_stmt_types.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema_scan.hpp"

#include <cctype>

namespace dudu {
namespace {

std::string receiver_base_type(std::string type) {
    type = trim_copy(std::move(type));
    while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
        type = trim_copy(type.substr(1));
    }
    for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
        const std::string prefix = std::string(wrapper) + "[";
        if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
            return receiver_base_type(type.substr(prefix.size(), type.size() - prefix.size() - 1));
        }
    }
    const size_t bracket = type.find('[');
    return bracket == std::string::npos ? type : trim_copy(type.substr(0, bracket));
}

bool looks_like_dudu_type(const std::string& name) {
    return !name.empty() && name.find('.') == std::string::npos &&
           std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

} // namespace

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
        if (looks_like_dudu_type(callee)) {
            return callee;
        }
        const size_t dot = callee.rfind('.');
        if (dot != std::string::npos) {
            const std::string receiver = trim_copy(callee.substr(0, dot));
            const auto local = locals.find(receiver);
            if (local != locals.end()) {
                const std::string method_name = trim_copy(callee.substr(dot + 1));
                const std::string receiver_type = trim_copy(local->second);
                const std::string key = receiver_base_type(receiver_type) + "." + method_name;
                if (const auto method = function_returns.find(key);
                    method != function_returns.end()) {
                    return method->second;
                }
            }
        }
    }
    return {};
}

} // namespace dudu
