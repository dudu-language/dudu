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

} // namespace dudu
