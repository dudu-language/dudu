#include "dudu/sema_alloc.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_inheritance.hpp"

#include <utility>

namespace dudu {
namespace {

TypeRef pointer_type_ref_from_pointee(TypeRef pointee) {
    const SourceLocation location = pointee.location;
    return wrapped_type_ref(TypeKind::Pointer, std::move(pointee), location);
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
            throw CompileError(*location,
                               "cannot allocate abstract class: " + type_ref_text(type_ref));
        }
    }
    return has_type_ref(type_ref) ? pointer_type_ref_from_pointee(type_ref) : TypeRef{};
}

} // namespace

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
