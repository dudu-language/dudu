#include "dudu/sema_alloc.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_inheritance.hpp"

namespace dudu {
namespace {

TypeRef pointer_type_ref_from_pointee(TypeRef pointee) {
    TypeRef pointer;
    pointer.kind = TypeKind::Pointer;
    pointer.location = pointee.location;
    pointer.range = pointee.range;
    pointer.children.push_back(std::move(pointee));
    pointer.text = substitute_type_ref_text(pointer, {});
    return pointer;
}

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
    const std::string type = substitute_type_ref_text(type_ref, {});
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
    if (location != nullptr && name == "new") {
        const std::vector<std::string> missing = unimplemented_abstract_methods(symbols, type_ref);
        if (!missing.empty()) {
            throw CompileError(*location, "cannot allocate abstract class: " + type);
        }
    }
    return "*" + type;
}

std::optional<TypeRef> infer_allocation_call_type_ref_from_type_args(
    const Symbols& symbols, const SourceLocation* location, const std::string& callee,
    const std::vector<TypeRef>& type_args, const size_t arg_count) {
    if (callee != "new" && callee != "malloc") {
        return std::nullopt;
    }
    if (location != nullptr && type_args.size() != 1) {
        throw CompileError(*location, callee + " expects 1 type argument, got " +
                                          std::to_string(type_args.size()));
    }
    const TypeRef type_ref = type_args.size() == 1 ? type_args.front() : TypeRef{};
    const std::string type = has_type_ref(type_ref) ? substitute_type_ref_text(type_ref, {}) : "";
    if (location != nullptr && type_args.size() == 1) {
        if (const auto unknown = unknown_type_ref(symbols, type_ref)) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : type_ref.location;
            throw CompileError(error_location, "unknown allocation type: " + unknown->first);
        }
    }
    if (location != nullptr && callee == "malloc" && arg_count != 1) {
        throw CompileError(*location,
                           "malloc expects 1 count argument, got " + std::to_string(arg_count));
    }
    if (location != nullptr && callee == "new" && type_args.size() == 1) {
        const std::vector<std::string> missing = unimplemented_abstract_methods(symbols, type_ref);
        if (!missing.empty()) {
            throw CompileError(*location, "cannot allocate abstract class: " + type);
        }
    }
    return has_type_ref(type_ref) ? pointer_type_ref_from_pointee(type_ref) : TypeRef{};
}

} // namespace

std::optional<std::string> infer_cpp_escape_allocation_call(const Symbols& symbols,
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
    const auto type_ref =
        infer_allocation_call_type_ref(symbols, location, callee, type_args, arg_count);
    if (!type_ref) {
        return std::nullopt;
    }
    return substitute_type_ref_text(*type_ref, {});
}

std::optional<TypeRef> infer_allocation_call_type_ref(const Symbols& symbols,
                                                      const SourceLocation* location,
                                                      const std::string& callee,
                                                      const std::vector<TypeRef>& type_args,
                                                      const size_t arg_count) {
    return infer_allocation_call_type_ref_from_type_args(symbols, location, callee, type_args,
                                                         arg_count);
}

bool is_deallocation_call(std::string_view callee) {
    return callee == "delete" || callee == "free";
}

void check_deallocation_args(const SourceLocation& location, std::string_view callee,
                             const std::vector<TypeRef>& arg_types) {
    if (arg_types.size() != 1) {
        throw CompileError(location, std::string(callee) + " expects 1 pointer argument, got " +
                                         std::to_string(arg_types.size()));
    }
    const TypeRef& type = arg_types.front();
    if (type.kind != TypeKind::Pointer) {
        throw CompileError(location, std::string(callee) + " expects pointer, got " +
                                         substitute_type_ref_text(type, {}));
    }
}

} // namespace dudu
