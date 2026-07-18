#include "dudu/sema/sema_dudu_overloads.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <algorithm>
#include <set>

namespace dudu {
namespace {

struct Candidate {
    DuduFunctionOverload overload;
    bool generic = false;
    size_t specificity = 0;
};

size_t type_specificity(const TypeRef& type, const std::set<std::string>& generic_names) {
    size_t score = generic_names.contains(type_ref_head_name(type)) ? 0 : 1;
    for (const TypeRef& child : type.children) {
        score += type_specificity(child, generic_names);
    }
    return score;
}

size_t declaration_specificity(const FunctionDecl& declaration) {
    std::set<std::string> generic_names;
    for (const std::string& param : declaration.generic_params) {
        generic_names.insert(generic_param_base_name(param));
    }
    size_t score = 0;
    for (const ParamDecl& param : declaration.params) {
        score += type_specificity(param.type_ref, generic_names);
    }
    return score;
}

std::vector<Candidate> viable_candidates(const FunctionScope& scope, const std::string& callee,
                                         const std::vector<Expr>& args,
                                         const std::vector<const FunctionDecl*>& requested,
                                         const std::optional<std::vector<TypeRef>>& explicit_args) {
    std::vector<Candidate> candidates;
    std::vector<const FunctionDecl*> declarations = requested;
    if (declarations.empty()) {
        const auto found = scope.symbols.function_overload_decls.find(callee);
        if (found == scope.symbols.function_overload_decls.end()) {
            return candidates;
        }
        declarations = found->second;
    }
    for (const FunctionDecl* declaration : declarations) {
        if (declaration == nullptr) {
            continue;
        }
        Candidate candidate;
        candidate.overload.declaration = declaration;
        candidate.generic = !declaration->generic_params.empty();
        candidate.specificity = declaration_specificity(*declaration);
        if (candidate.generic) {
            std::optional<std::vector<TypeRef>> inferred = explicit_args;
            if (!inferred) {
                inferred = infer_generic_call_type_args(scope, *declaration, callee, args, nullptr);
            }
            if (!inferred) {
                continue;
            }
            if (!generic_arity_matches(declaration->generic_params, inferred->size())) {
                continue;
            }
            candidate.overload.generic_args = *inferred;
            candidate.overload.signature = instantiate_generic_signature(*declaration, *inferred);
        } else {
            if (explicit_args) {
                continue;
            }
            candidate.overload.signature = function_signature_from_decl(*declaration);
        }
        if (matching_signature_ast(scope, {candidate.overload.signature}, args)) {
            candidates.push_back(std::move(candidate));
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& left,
                                                              const Candidate& right) {
        if (left.generic != right.generic) {
            return !left.generic;
        }
        return left.specificity > right.specificity;
    });
    return candidates;
}

} // namespace

std::optional<DuduFunctionOverload>
select_dudu_function_overload(const FunctionScope& scope, const std::string& callee,
                              const std::vector<Expr>& args,
                              const std::vector<const FunctionDecl*>& declarations,
                              const std::optional<std::vector<TypeRef>>& explicit_type_args) {
    std::vector<Candidate> candidates =
        viable_candidates(scope, callee, args, declarations, explicit_type_args);
    if (candidates.empty()) {
        return std::nullopt;
    }
    std::vector<FunctionSignature> signatures;
    signatures.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        signatures.push_back(candidate.overload.signature);
    }
    const std::optional<size_t> selected = matching_signature_index_ast(scope, signatures, args);
    if (!selected) {
        return std::nullopt;
    }
    return candidates[*selected].overload;
}

} // namespace dudu
