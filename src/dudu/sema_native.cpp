#include "dudu/sema_native.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

#include <cctype>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool arity_matches(const FunctionSignature& signature, size_t arg_count) {
    return signature.variadic ? arg_count >= signature.params.size()
                              : arg_count == signature.params.size();
}

bool native_numeric_promotion(const std::string& expected, const std::string& got) {
    return expected == "f64" && got == "f32";
}

std::string replace_type_identifier(std::string type, const std::string& name,
                                    const std::string& arg) {
    if (name.empty() || arg.empty()) {
        return type;
    }
    size_t pos = type.find(name);
    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(type[pos - 1])) == 0 && type[pos - 1] != '_');
        const size_t end = pos + name.size();
        const bool right_ok =
            end >= type.size() ||
            (std::isalnum(static_cast<unsigned char>(type[end])) == 0 && type[end] != '_');
        if (left_ok && right_ok) {
            type.replace(pos, name.size(), arg);
            pos = type.find(name, pos + arg.size());
        } else {
            pos = type.find(name, pos + 1);
        }
    }
    return type;
}

std::string replace_template_types(std::string type, const std::vector<std::string>& args) {
    static const std::vector<std::string> names = {"T", "U", "V", "A", "B", "Key", "Value"};
    for (size_t i = 0; i < args.size() && i < names.size(); ++i) {
        type = replace_type_identifier(std::move(type), names[i], args[i]);
    }
    return type;
}

FunctionSignature substitute_template_signature(FunctionSignature signature,
                                                const std::vector<std::string>& args) {
    for (std::string& param : signature.params) {
        param = replace_template_types(std::move(param), args);
    }
    signature.return_type = replace_template_types(std::move(signature.return_type), args);
    return signature;
}

std::optional<std::pair<std::string, std::vector<std::string>>>
template_call_base(const std::string& callee) {
    const size_t close = callee.rfind(']');
    if (close == std::string::npos || close + 1 != callee.size()) {
        return std::nullopt;
    }
    int depth = 0;
    for (size_t i = close + 1; i > 0; --i) {
        const size_t pos = i - 1;
        if (callee[pos] == ']') {
            ++depth;
        } else if (callee[pos] == '[') {
            --depth;
            if (depth == 0) {
                const std::string base = trim(callee.substr(0, pos));
                std::vector<std::string> args =
                    split_top_level_args(callee.substr(pos + 1, close - pos - 1));
                for (std::string& arg : args) {
                    arg = trim(std::move(arg));
                }
                if (!base.empty() && !args.empty()) {
                    return std::make_pair(base, std::move(args));
                }
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

std::string signature_text(const std::string& callee, const FunctionSignature& signature) {
    std::ostringstream out;
    out << callee << "(";
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << signature.params[i];
    }
    if (signature.variadic) {
        if (!signature.params.empty())
            out << ", ";
        out << "...";
    }
    out << ") -> " << signature.return_type;
    return out.str();
}

std::string native_overload_message_ast(const FunctionScope& scope, const std::string& callee,
                                        const std::vector<Expr>& args,
                                        const std::vector<FunctionSignature>& candidates,
                                        const SourceLocation* location,
                                        const NativeInferExprAstFn& infer_expr) {
    std::ostringstream out;
    out << "no native overload of " << callee << " accepts " << args.size() << " arguments";
    if (!args.empty()) {
        out << "\narguments: ";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0)
                out << ", ";
            out << infer_expr(scope, args[i], location);
        }
    }
    for (const FunctionSignature& candidate : candidates) {
        out << "\ncandidate: " << signature_text(callee, candidate);
    }
    return out.str();
}

bool args_match_signature_ast(const FunctionScope& scope, const FunctionSignature& signature,
                              const std::vector<Expr>& args, const SourceLocation* location,
                              const NativeInferExprAstFn& infer_expr,
                              const NativeCanAssignAstFn& can_assign) {
    if (!arity_matches(signature, args.size())) {
        return false;
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string got = infer_expr(scope, args[i], location);
        if (!can_assign(signature.params[i], args[i], got) &&
            !native_numeric_promotion(signature.params[i], got)) {
            return false;
        }
    }
    return true;
}

bool explicit_native_template_allowed(const FunctionScope& scope, const std::string& lookup,
                                      bool explicit_template_call) {
    if (!explicit_template_call) {
        return false;
    }
    const size_t dot = lookup.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    return scope.symbols.native_explicit_template_prefixes.contains(lookup.substr(0, dot));
}

} // namespace

std::optional<FunctionSignature> native_signature_for_call(const FunctionScope& scope,
                                                           const std::string& callee,
                                                           const std::vector<Expr>& args,
                                                           const SourceLocation* location,
                                                           const NativeInferExprAstFn& infer_expr,
                                                           const NativeCanAssignAstFn& can_assign) {
    const auto template_call = template_call_base(callee);
    const std::string lookup = template_call ? template_call->first : callee;
    const auto found = scope.symbols.native_function_signatures.find(lookup);
    if (found == scope.symbols.native_function_signatures.end()) {
        return std::nullopt;
    }
    std::vector<FunctionSignature> candidates = found->second;
    if (template_call) {
        for (FunctionSignature& signature : candidates) {
            signature = substitute_template_signature(std::move(signature), template_call->second);
        }
    }
    for (const FunctionSignature& signature : candidates) {
        if (args_match_signature_ast(scope, signature, args, location, infer_expr, can_assign)) {
            return signature;
        }
    }
    bool has_variadic_candidate = false;
    for (const FunctionSignature& signature : candidates) {
        has_variadic_candidate = has_variadic_candidate || signature.variadic;
    }
    if (!has_variadic_candidate &&
        explicit_native_template_allowed(scope, lookup, template_call.has_value())) {
        for (const Expr& arg : args) {
            (void)infer_expr(scope, arg, location);
        }
        FunctionSignature signature;
        signature.return_type = "auto";
        return signature;
    }
    if (location != nullptr) {
        fail(*location,
             native_overload_message_ast(scope, callee, args, candidates, location, infer_expr));
    }
    return std::nullopt;
}

} // namespace dudu
