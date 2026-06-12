#include "dudu/sema_alloc.hpp"

#include "dudu/cpp_lower.hpp"

namespace dudu {

std::optional<std::string> infer_allocation_call(const Symbols& symbols,
                                                 const SourceLocation* location,
                                                 const std::string& callee,
                                                 const std::vector<std::string>& args) {
    const size_t open = callee.find('[');
    if (open == std::string::npos || callee.back() != ']') {
        return std::nullopt;
    }
    const std::string name = callee.substr(0, open);
    if (name != "new" && name != "malloc") {
        return std::nullopt;
    }
    const std::string type = trim_copy(callee.substr(open + 1, callee.size() - open - 2));
    if (location != nullptr && !known_type(symbols, type)) {
        throw CompileError(*location, "unknown allocation type: " + type);
    }
    if (location != nullptr && name == "malloc" && args.size() != 1) {
        throw CompileError(*location,
                           "malloc expects 1 count argument, got " + std::to_string(args.size()));
    }
    return "*" + type;
}

bool is_deallocation_call(std::string_view callee) {
    return callee == "delete" || callee == "free";
}

void check_deallocation_args(const SourceLocation& location, std::string_view callee,
                             const std::vector<std::string>& arg_types) {
    if (arg_types.size() != 1) {
        throw CompileError(location, std::string(callee) + " expects 1 pointer argument, got " +
                                         std::to_string(arg_types.size()));
    }
    const std::string type = trim_copy(arg_types.front());
    if (type.empty() || type.front() != '*') {
        throw CompileError(location, std::string(callee) + " expects pointer, got " + type);
    }
}

} // namespace dudu
