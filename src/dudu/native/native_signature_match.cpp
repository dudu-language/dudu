#include "dudu/native/native_signature_match.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/native/native_signature_match_internal.hpp"

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

} // namespace

std::optional<NativeSignatureMatch>
match_native_signature_declaration(const FunctionScope& scope, const std::string& callee,
                                   const std::vector<TypeRef>& explicit_template_args,
                                   const std::vector<Expr>& args, const SourceLocation* location) {
    const auto found = scope.symbols.native_function_signatures.find(callee);
    if (found == scope.symbols.native_function_signatures.end()) {
        return std::nullopt;
    }
    native_match_internal::NativeCandidateSelection selection =
        native_match_internal::select_native_signature_candidates(
            scope, found->second, explicit_template_args, args, location);
    if (selection.signature) {
        if (selection.equally_ranked.size() > 1) {
            if (location != nullptr) {
                fail(*location, native_match_internal::ambiguous_native_overload_message(
                                    callee, selection.arg_type_refs, selection.equally_ranked));
            }
            return std::nullopt;
        }
        const NativeFunctionDecl* declaration =
            native_function_decl_for_overload(scope.symbols, callee, selection.index);
        return NativeSignatureMatch{.signature = std::move(*selection.signature),
                                    .declaration = declaration,
                                    .inferred_template_args =
                                        std::move(selection.inferred_template_args)};
    }
    if (location != nullptr) {
        fail(*location, native_match_internal::native_overload_message_ast(
                            scope, callee, args, selection.arg_type_refs, selection.candidates,
                            selection.mismatches));
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
match_native_signature(const FunctionScope& scope, const std::string& callee,
                       const std::vector<TypeRef>& explicit_template_args,
                       const std::vector<Expr>& args, const SourceLocation* location) {
    const std::optional<NativeSignatureMatch> matched =
        match_native_signature_declaration(scope, callee, explicit_template_args, args, location);
    return matched ? std::optional<FunctionSignature>{matched->signature} : std::nullopt;
}

std::optional<FunctionSignature>
match_native_method_signature(const FunctionScope& scope, const std::string& callee,
                              const std::vector<FunctionSignature>& candidates,
                              const std::vector<TypeRef>& explicit_template_args,
                              const Expr& receiver, const std::vector<Expr>& args,
                              const SourceLocation* location) {
    std::vector<FunctionSignature> expanded;
    expanded.reserve(candidates.size());
    for (const FunctionSignature& candidate : candidates) {
        if (!has_type_ref(candidate.receiver_type_ref)) {
            continue;
        }
        expanded.push_back(native_match_internal::signature_with_implicit_receiver(candidate));
    }
    std::vector<Expr> call_args;
    call_args.reserve(args.size() + 1);
    call_args.push_back(receiver);
    call_args.insert(call_args.end(), args.begin(), args.end());
    native_match_internal::NativeCandidateSelection selection =
        native_match_internal::select_native_signature_candidates(
            scope, std::move(expanded), explicit_template_args, call_args, location);
    if (selection.signature && selection.equally_ranked.size() == 1) {
        return native_match_internal::signature_without_implicit_receiver(
            std::move(*selection.signature));
    }
    if (location != nullptr) {
        std::vector<TypeRef> explicit_arg_types = selection.arg_type_refs;
        if (!explicit_arg_types.empty()) {
            explicit_arg_types.erase(explicit_arg_types.begin());
        }
        if (selection.signature) {
            std::vector<std::pair<size_t, FunctionSignature>> explicit_matches =
                selection.equally_ranked;
            for (auto& [candidate_index, signature] : explicit_matches) {
                (void)candidate_index;
                signature = native_match_internal::signature_without_implicit_receiver(
                    std::move(signature));
            }
            fail(*location, native_match_internal::ambiguous_native_overload_message(
                                callee, explicit_arg_types, explicit_matches));
        }
        std::vector<FunctionSignature> explicit_candidates = std::move(selection.candidates);
        for (FunctionSignature& candidate : explicit_candidates) {
            candidate =
                native_match_internal::signature_without_implicit_receiver(std::move(candidate));
        }
        fail(*location, native_match_internal::native_overload_message_ast(
                            scope, callee, args, explicit_arg_types, explicit_candidates,
                            selection.mismatches));
    }
    return std::nullopt;
}

} // namespace dudu
