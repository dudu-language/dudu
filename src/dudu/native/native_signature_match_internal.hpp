#pragma once

#include "dudu/native/native_signature_match.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dudu::native_match_internal {

struct NativeCandidateSelection {
    std::optional<FunctionSignature> signature;
    size_t index = 0;
    std::vector<TypeRef> arg_type_refs;
    std::vector<FunctionSignature> candidates;
    std::vector<std::optional<std::string>> mismatches;
    std::vector<std::pair<size_t, FunctionSignature>> equally_ranked;
};

NativeCandidateSelection select_native_signature_candidates(
    const FunctionScope& scope, std::vector<FunctionSignature> candidates,
    const std::vector<TypeRef>& explicit_template_args, const std::vector<Expr>& args,
    const SourceLocation* location);

std::string native_overload_message_ast(
    const FunctionScope& scope, const std::string& callee, const std::vector<Expr>& args,
    const std::vector<TypeRef>& arg_type_refs,
    const std::vector<FunctionSignature>& candidates,
    const std::vector<std::optional<std::string>>& explicit_template_mismatches);

std::string ambiguous_native_overload_message(
    const std::string& callee, const std::vector<TypeRef>& arg_type_refs,
    const std::vector<std::pair<size_t, FunctionSignature>>& matches);

FunctionSignature signature_with_implicit_receiver(FunctionSignature signature);
FunctionSignature signature_without_implicit_receiver(FunctionSignature signature);

} // namespace dudu::native_match_internal
