#include "dudu/sema_function_type.hpp"

#include "dudu/cpp_lower.hpp"

#include <sstream>

namespace dudu {

std::string function_type(const FunctionSignature& signature) {
    std::ostringstream out;
    out << "fn(";
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << signature.params[i];
    }
    out << ") -> " << signature.return_type;
    return out.str();
}

bool parse_function_type(std::string type, FunctionSignature& out) {
    type = trim_copy(std::move(type));
    const size_t open = type.find('[');
    if (open != std::string::npos && type.back() == ']') {
        const std::string inner = trim_copy(type.substr(open + 1, type.size() - open - 2));
        if (starts_with(inner, "fn(")) {
            type = inner;
        }
    }
    if (!starts_with(type, "fn(")) {
        return false;
    }
    const size_t close = type.find(')');
    const size_t arrow = type.find("->", close == std::string::npos ? 0 : close);
    if (close == std::string::npos) {
        return false;
    }
    out.params.clear();
    const std::string args = trim_copy(type.substr(3, close - 3));
    if (!args.empty()) {
        out.params = split_top_level_args(args);
    }
    out.return_type = arrow == std::string::npos ? "void" : trim_copy(type.substr(arrow + 2));
    if (out.return_type.empty()) {
        out.return_type = "void";
    }
    return true;
}

} // namespace dudu
