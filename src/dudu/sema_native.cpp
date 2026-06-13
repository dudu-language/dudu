#include "dudu/sema_native.hpp"

#include "dudu/source.hpp"

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool arity_matches(const FunctionSignature& signature, size_t arg_count) {
    return signature.variadic ? arg_count >= signature.params.size()
                              : arg_count <= signature.params.size();
}

bool args_match_signature(const FunctionScope& scope, const FunctionSignature& signature,
                          const std::vector<std::string>& args, const SourceLocation* location,
                          const NativeInferExprFn& infer_expr,
                          const NativeCanAssignFn& can_assign) {
    if (!arity_matches(signature, args.size())) {
        return false;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string got = infer_expr(scope, args[i], location);
        if (!can_assign(signature.params[i], args[i], got)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<FunctionSignature> native_signature_for_call(
    const FunctionScope& scope, const std::string& callee, const std::vector<std::string>& args,
    const SourceLocation* location, const NativeInferExprFn& infer_expr,
    const NativeCanAssignFn& can_assign) {
    const auto found = scope.symbols.native_function_signatures.find(callee);
    if (found == scope.symbols.native_function_signatures.end()) {
        return std::nullopt;
    }
    for (const FunctionSignature& signature : found->second) {
        if (args_match_signature(scope, signature, args, location, infer_expr, can_assign)) {
            return signature;
        }
    }
    if (location != nullptr) {
        fail(*location, "no native overload of " + callee + " accepts " +
                            std::to_string(args.size()) + " arguments");
    }
    return std::nullopt;
}

} // namespace dudu
