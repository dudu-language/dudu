#include "dudu/sema_alloc.hpp"

#include "dudu/cpp_lower.hpp"

namespace dudu {
namespace {

std::optional<std::string> infer_allocation_call_with_count(const Symbols& symbols,
                                                            const SourceLocation* location,
                                                            const std::string& callee,
                                                            const size_t arg_count) {
    const size_t open = callee.find('[');
    if (open == std::string::npos || callee.back() != ']') {
        return std::nullopt;
    }
    const std::string name = callee.substr(0, open);
    if (name != "new" && name != "malloc") {
        return std::nullopt;
    }
    SourceLocation type_location;
    if (location != nullptr) {
        type_location = *location;
        type_location.column += static_cast<int>(open + 1);
    }
    const TypeRef type_ref =
        parse_type_text(callee.substr(open + 1, callee.size() - open - 2), type_location);
    const std::string type = trim_copy(type_ref.text);
    if (location != nullptr) {
        if (const auto unknown = unknown_type_ref(symbols, type_ref)) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : type_location;
            throw CompileError(error_location, "unknown allocation type: " + unknown->first);
        }
    }
    if (location != nullptr && name == "malloc" && arg_count != 1) {
        throw CompileError(*location,
                           "malloc expects 1 count argument, got " + std::to_string(arg_count));
    }
    return "*" + type;
}

std::optional<std::string> infer_allocation_call_from_type_args(
    const Symbols& symbols, const SourceLocation* location, const std::string& callee,
    const std::vector<TypeRef>& type_args, const size_t arg_count) {
    if (callee != "new" && callee != "malloc") {
        return std::nullopt;
    }
    if (location != nullptr && type_args.size() != 1) {
        throw CompileError(*location, callee + " expects 1 type argument, got " +
                                          std::to_string(type_args.size()));
    }
    const std::string type = type_args.size() == 1 ? trim_copy(type_args.front().text) : "";
    if (location != nullptr && type_args.size() == 1) {
        if (const auto unknown = unknown_type_ref(symbols, type_args.front())) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : type_args.front().location;
            throw CompileError(error_location, "unknown allocation type: " + unknown->first);
        }
    }
    if (location != nullptr && callee == "malloc" && arg_count != 1) {
        throw CompileError(*location,
                           "malloc expects 1 count argument, got " + std::to_string(arg_count));
    }
    return "*" + type;
}

} // namespace

std::optional<std::string> infer_allocation_call(const Symbols& symbols,
                                                 const SourceLocation* location,
                                                 const std::string& callee,
                                                 const std::vector<std::string>& args) {
    return infer_allocation_call_with_count(symbols, location, callee, args.size());
}

std::optional<std::string> infer_allocation_call(const Symbols& symbols,
                                                 const SourceLocation* location,
                                                 const std::string& callee,
                                                 const std::vector<Expr>& args) {
    return infer_allocation_call_with_count(symbols, location, callee, args.size());
}

std::optional<std::string> infer_allocation_call(const Symbols& symbols,
                                                 const SourceLocation* location,
                                                 const std::string& callee,
                                                 const std::vector<TypeRef>& type_args,
                                                 const size_t arg_count) {
    return infer_allocation_call_from_type_args(symbols, location, callee, type_args, arg_count);
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
