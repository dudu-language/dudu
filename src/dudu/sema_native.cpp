#include "dudu/sema_native.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/native_signature_match.hpp"

#include <optional>

namespace dudu {

bool foreign_cpp_type_name(const Symbols& symbols, const std::string& type) {
    return type.find('.') != std::string::npos || type.find("::") != std::string::npos ||
           symbols.native_types.contains(base_type(type));
}

bool native_import_path_prefix(const Symbols& symbols, const std::string& path) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    return symbols.native_import_prefixes.contains(path.substr(0, dot));
}

std::optional<std::string> native_member_path_type(const Symbols& symbols,
                                                   const std::string& path) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        return std::nullopt;
    }
    const std::string prefix = path.substr(0, dot);
    if (prefix == "build" || prefix == "shader") {
        return "auto";
    }
    if (!native_import_path_prefix(symbols, path)) {
        return std::nullopt;
    }
    if (const auto value = symbols.native_values.find(path); value != symbols.native_values.end()) {
        return value->second;
    }
    return "auto";
}

std::optional<FunctionSignature> native_signature_for_call(const FunctionScope& scope,
                                                           const std::string& callee,
                                                           const std::vector<Expr>& args,
                                                           const SourceLocation* location,
                                                           const NativeInferExprAstFn& infer_expr,
                                                           const NativeCanAssignAstFn& can_assign) {
    return match_native_signature(scope, callee, args, location, infer_expr, can_assign);
}

} // namespace dudu
