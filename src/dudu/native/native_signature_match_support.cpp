#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/native/native_signature_match_internal.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/native/native_signature_templates.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_inheritance.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <tuple>

namespace dudu::native_match_internal {

bool native_variadic_pack_param(const TypeRef& type) {
    return type.kind == TypeKind::PackExpansion;
}

size_t native_variadic_pack_start(const FunctionSignature& signature) {
    if (!signature.variadic) {
        return signature_param_count(signature);
    }
    size_t start = signature_param_count(signature);
    while (start > 0 &&
           native_variadic_pack_param(signature_param_type_ref(signature, start - 1))) {
        --start;
    }
    return start;
}

std::optional<std::string> native_variadic_pack_placeholder(const FunctionSignature& signature,
                                                            size_t pack_start) {
    const NativeTemplateParameterNames template_params = native_template_parameters(signature);
    for (size_t i = pack_start; i < signature_param_count(signature); ++i) {
        if (const std::optional<std::string> pack = native_template_pack_placeholder(
                signature_param_type_ref(signature, i), template_params)) {
            return pack;
        }
    }
    return std::nullopt;
}

size_t minimum_runtime_params(const FunctionSignature& signature) {
    const size_t param_count = signature_param_count(signature);
    size_t minimum =
        signature.min_params < 0 ? param_count : static_cast<size_t>(signature.min_params);
    const size_t pack_start = native_variadic_pack_start(signature);
    if (pack_start < param_count) {
        minimum = std::min(minimum, pack_start);
    }
    return minimum;
}

bool arity_matches(const FunctionSignature& signature, size_t arg_count) {
    const size_t param_count = signature_param_count(signature);
    const size_t min_params = minimum_runtime_params(signature);
    return signature.variadic ? arg_count >= min_params
                              : arg_count >= min_params && arg_count <= param_count;
}

bool template_param_pack(std::string_view name) {
    return name.ends_with("...");
}

std::string template_param_name(std::string name) {
    if (name.ends_with("...")) {
        name.resize(name.size() - 3);
    }
    return name;
}

void bind_default_template_args(const FunctionSignature& signature,
                                NativeTemplateBindings& bindings) {
    const size_t count =
        std::min(signature.template_params.size(), signature.template_default_args.size());
    for (size_t i = 0; i < count; ++i) {
        const std::string name = template_param_name(signature.template_params[i]);
        if (bindings.contains(name) || !has_type_ref(signature.template_default_args[i])) {
            continue;
        }
        bindings.emplace(name, substitute_type_ref(signature.template_default_args[i], bindings));
    }
}

TypeRef parameter_with_available_defaults(const Symbols& symbols,
                                          const FunctionSignature& signature,
                                          const TypeRef& parameter,
                                          const NativeTemplateBindings& bindings) {
    NativeTemplateBindings available = bindings;
    bind_default_template_args(signature, available);
    return resolve_associated_type_ref(symbols, substitute_type_ref(parameter, available));
}

std::optional<std::string> explicit_template_mismatch(const Symbols& symbols,
                                                      const FunctionSignature& signature,
                                                      const std::vector<TypeRef>& explicit_args) {
    if (explicit_args.empty()) {
        return std::nullopt;
    }
    if (signature.template_params.empty()) {
        return "function is not a template";
    }
    if (signature.template_param_is_value.size() != signature.template_params.size()) {
        return "native template parameter metadata is incomplete";
    }
    const bool trailing_pack = template_param_pack(signature.template_params.back());
    if (explicit_args.size() > signature.template_params.size() && !trailing_pack) {
        return "template accepts at most " + std::to_string(signature.template_params.size()) +
               " explicit arguments, got " + std::to_string(explicit_args.size());
    }
    for (size_t i = 0; i < explicit_args.size(); ++i) {
        const size_t param_index = std::min(i, signature.template_param_is_value.size() - 1);
        const TypeRef& arg = explicit_args[i];
        const bool expects_value = signature.template_param_is_value[param_index];
        const bool is_literal_value = arg.kind == TypeKind::Value;
        const bool is_known_type = known_type_ref(symbols, arg);
        if (expects_value && is_known_type) {
            return "template argument " + std::to_string(i + 1) + " expects a value, got type " +
                   type_ref_text(arg);
        }
        if (!expects_value && is_literal_value) {
            return "template argument " + std::to_string(i + 1) + " expects a type, got value " +
                   type_ref_text(arg);
        }
    }
    return std::nullopt;
}

bool native_numeric_promotion(const TypeRef& expected, const TypeRef& got) {
    if (!has_type_ref(expected) || !has_type_ref(got)) {
        return false;
    }
    return type_ref_head_name(expected) == "f64" && type_ref_head_name(got) == "f32";
}

bool native_const_reference(const TypeRef& type) {
    return type.kind == TypeKind::Reference && type.children.size() == 1 &&
           type.children.front().kind == TypeKind::Const;
}

const TypeRef& native_referent(const TypeRef& type) {
    if (type.kind == TypeKind::Reference && type.children.size() == 1) {
        return type.children.front();
    }
    return type;
}

TypeRef native_unqualified_referent(TypeRef type) {
    if (type.kind == TypeKind::Reference && type.children.size() == 1) {
        type = type.children.front();
    }
    while ((type.kind == TypeKind::Const || type.kind == TypeKind::Volatile) &&
           type.children.size() == 1) {
        type = type.children.front();
    }
    return type;
}

TypeRef concrete_native_argument_type_ref(const Symbols& symbols, TypeRef type) {
    type = resolve_associated_type_ref(symbols, std::move(type));
    if (type.kind == TypeKind::Named || type.kind == TypeKind::Qualified ||
        type.kind == TypeKind::Template) {
        return receiver_template_type_ref(symbols, std::move(type));
    }
    for (TypeRef& child : type.children) {
        child = concrete_native_argument_type_ref(symbols, std::move(child));
    }
    return type;
}

bool native_referent_is_const(const TypeRef& type) {
    return native_referent(type).kind == TypeKind::Const;
}

bool native_forwarding_reference(const TypeRef& type,
                                 const NativeTemplateParameterNames& template_params) {
    return type.kind == TypeKind::Reference && type.reference_kind == ReferenceKind::Rvalue &&
           type.children.size() == 1 &&
           template_params.contains(type_ref_head_name(type.children.front()));
}

bool native_arg_is_lvalue(const Expr& arg, const TypeRef& got) {
    if (got.kind == TypeKind::Reference) {
        return got.reference_kind == ReferenceKind::Lvalue;
    }
    return expr_is_lvalue(arg);
}

bool native_reference_binding_allowed(const TypeRef& expected, const Expr& arg, const TypeRef& got,
                                      const NativeTemplateParameterNames& template_params) {
    if (expected.kind != TypeKind::Reference) {
        return true;
    }
    const bool lvalue = native_arg_is_lvalue(arg, got);
    if (expected.reference_kind == ReferenceKind::Rvalue) {
        return !lvalue || native_forwarding_reference(expected, template_params);
    }
    return lvalue || native_const_reference(expected);
}

TypeRef native_template_binding_arg(const TypeRef& expected, const TypeRef& got, const Expr& arg,
                                    const NativeTemplateParameterNames& template_params) {
    if (!native_arg_is_lvalue(arg, got) ||
        !native_forwarding_reference(expected, template_params)) {
        return got;
    }
    TypeRef reference;
    reference.kind = TypeKind::Reference;
    reference.children.push_back(got);
    reference.location = got.location;
    reference.range = got.range;
    return reference;
}

std::optional<TypeRef> fixed_array_element_type_ref(const TypeRef& type) {
    if (type.kind != TypeKind::FixedArray || type.children.empty()) {
        return std::nullopt;
    }
    const TypeRef& storage = type.children.front();
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        return storage.children.front();
    }
    return storage;
}

std::optional<TypeRef> pointer_pointee_type_ref(const TypeRef& type) {
    if (type.kind != TypeKind::Pointer || type.children.size() != 1) {
        return std::nullopt;
    }
    return type.children.front();
}

bool native_fixed_array_alias_decay_allowed(const FunctionScope& scope, const TypeRef& expected,
                                            const TypeRef& got) {
    const std::string got_name = type_ref_head_name(got);
    if (got_name.empty() || !scope.symbols.native_types.contains(got_name)) {
        return false;
    }
    const std::optional<TypeRef> expected_pointee = pointer_pointee_type_ref(expected);
    if (!expected_pointee) {
        return false;
    }
    const TypeRef resolved_got = resolve_alias_ref(scope.symbols, got);
    const std::optional<TypeRef> got_element = fixed_array_element_type_ref(resolved_got);
    if (!got_element) {
        return false;
    }
    if (type_assignment_allowed(*expected_pointee, *got_element)) {
        return true;
    }
    if (expected_pointee->kind == TypeKind::Const && expected_pointee->children.size() == 1) {
        return type_assignment_allowed(expected_pointee->children.front(), *got_element);
    }
    return false;
}

std::string signature_display(const std::string& callee, const FunctionSignature& signature) {
    std::ostringstream out;
    out << callee << "(";
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        if (i > 0)
            out << ", ";
        out << type_ref_text(signature_param_type_ref(signature, i));
    }
    if (signature.variadic) {
        if (param_count != 0)
            out << ", ";
        out << "...";
    }
    if (signature.min_params >= 0 && static_cast<size_t>(signature.min_params) < param_count) {
        out << "; min " << signature.min_params;
    }
    out << ") -> " << type_ref_text(signature_return_type_ref(signature));
    return out.str();
}

bool native_arg_assignable(const FunctionScope& scope, const Expr& arg, const TypeRef& got_ref,
                           const TypeRef& expected_ref) {
    if (!has_type_ref(expected_ref) || !has_type_ref(got_ref)) {
        return false;
    }
    if (expected_ref.kind == TypeKind::Reference && !native_referent_is_const(expected_ref) &&
        native_referent_is_const(got_ref)) {
        return false;
    }
    if (native_fixed_array_alias_decay_allowed(scope, native_referent(expected_ref), got_ref)) {
        return true;
    }
    if (can_assign_ast(scope, native_referent(expected_ref), arg, native_referent(got_ref))) {
        return true;
    }
    const TypeRef expected_value =
        concrete_native_argument_type_ref(scope.symbols, native_referent(expected_ref));
    const TypeRef got_value =
        concrete_native_argument_type_ref(scope.symbols, native_referent(got_ref));
    if (type_assignment_allowed(expected_value, got_value)) {
        return true;
    }
    const TypeRef expected_unqualified =
        concrete_native_argument_type_ref(scope.symbols,
                                          native_unqualified_referent(expected_ref));
    const TypeRef got_unqualified =
        concrete_native_argument_type_ref(scope.symbols, native_unqualified_referent(got_ref));
    if (type_assignment_allowed(expected_unqualified, got_unqualified)) {
        return true;
    }
    if (native_base_assignable(scope.symbols, expected_ref, got_ref)) {
        return true;
    }
    return can_assign_ast(scope, expected_value, arg, got_value);
}

std::vector<std::string> mismatch_reasons_ast(const FunctionScope& scope,
                                              const FunctionSignature& signature,
                                              const std::vector<Expr>& args,
                                              const std::vector<TypeRef>& arg_type_refs) {
    if (!arity_matches(signature, args.size())) {
        std::ostringstream out;
        out << "arity expects ";
        const size_t param_count = signature_param_count(signature);
        if (signature.variadic) {
            const size_t min_params = minimum_runtime_params(signature);
            out << "at least " << min_params;
        } else if (signature.min_params >= 0 &&
                   static_cast<size_t>(signature.min_params) < param_count) {
            out << signature.min_params << " to " << param_count;
        } else {
            out << param_count;
        }
        out << " arguments, got " << args.size();
        return {out.str()};
    }

    std::vector<std::string> reasons;
    const NativeTemplateParameterNames template_params = native_template_parameters(signature);
    const size_t fixed_params = std::min(signature_param_count(signature), args.size());
    for (size_t i = 0; i < fixed_params; ++i) {
        const TypeRef& got_ref = arg_type_refs[i];
        const TypeRef expected_ref = signature_param_type_ref(signature, i);
        const std::string got_display = substitute_type_ref_text(got_ref, {});
        if (!native_reference_binding_allowed(expected_ref, args[i], got_ref, template_params)) {
            std::ostringstream out;
            out << "parameter " << (i + 1) << " expects " << type_ref_text(expected_ref) << ", got "
                << (native_arg_is_lvalue(args[i], got_ref) ? "lvalue " : "rvalue ") << got_display;
            reasons.push_back(out.str());
            continue;
        }
        if (!native_arg_assignable(scope, args[i], got_ref, expected_ref) &&
            !native_numeric_promotion(expected_ref, got_ref)) {
            std::ostringstream out;
            out << "parameter " << (i + 1) << " expects " << type_ref_text(expected_ref) << ", got "
                << got_display;
            reasons.push_back(out.str());
        }
    }
    return reasons;
}

std::string native_overload_message_ast(
    const FunctionScope& scope, const std::string& callee, const std::vector<Expr>& args,
    const std::vector<TypeRef>& arg_type_refs, const std::vector<FunctionSignature>& candidates,
    const std::vector<std::optional<std::string>>& explicit_template_mismatches) {
    std::ostringstream out;
    out << "no native overload of " << callee << " accepts " << args.size() << " arguments";
    if (!args.empty()) {
        out << "\narguments: ";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0)
                out << ", ";
            out << substitute_type_ref_text(arg_type_refs[i], {});
        }
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
        const FunctionSignature& candidate = candidates[i];
        out << "\ncandidate: " << signature_display(callee, candidate);
        if (i < explicit_template_mismatches.size() && explicit_template_mismatches[i]) {
            out << "\n  reason: " << *explicit_template_mismatches[i];
            continue;
        }
        for (const std::string& reason :
             mismatch_reasons_ast(scope, candidate, args, arg_type_refs)) {
            out << "\n  reason: " << reason;
        }
    }
    return out.str();
}

struct MatchedNativeSignature {
    FunctionSignature signature;
    std::vector<TypeRef> inferred_template_args;
};

std::vector<TypeRef> ordered_template_args(const FunctionSignature& signature,
                                           const NativeTemplateBindings& bindings,
                                           const NativePackBindingMap& pack_bindings) {
    std::vector<TypeRef> out;
    for (const std::string& param : signature.template_params) {
        const std::string name = template_param_name(param);
        if (template_param_pack(param)) {
            const auto found = pack_bindings.find(name);
            if (found == pack_bindings.end()) {
                return {};
            }
            out.insert(out.end(), found->second.begin(), found->second.end());
            continue;
        }
        const auto found = bindings.find(name);
        if (found == bindings.end()) {
            return {};
        }
        out.push_back(found->second);
    }
    return out;
}

std::optional<MatchedNativeSignature>
match_signature_ast(const FunctionScope& scope, const FunctionSignature& signature,
                    const std::vector<Expr>& args, const std::vector<TypeRef>& arg_type_refs) {
    NativeTemplateBindings bindings;
    NativePackBindingMap pack_bindings;
    const NativeTemplateParameterNames template_params = native_template_parameters(signature);
    const size_t param_count = signature_param_count(signature);
    const size_t pack_start = native_variadic_pack_start(signature);
    const bool has_pack_param = pack_start < param_count;
    const std::optional<std::string> pack_param_placeholder =
        has_pack_param ? native_variadic_pack_placeholder(signature, pack_start) : std::nullopt;
    FunctionSignature arity_signature = signature;
    if (has_pack_param) {
        arity_signature.min_params = static_cast<int>(pack_start);
    }
    if (!arity_matches(arity_signature, args.size())) {
        return std::nullopt;
    }
    const size_t fixed_params = has_pack_param ? pack_start : signature_param_count(signature);
    const size_t provided_fixed = std::min(fixed_params, args.size());
    for (size_t i = 0; i < provided_fixed; ++i) {
        const TypeRef& got_ref = arg_type_refs[i];
        const TypeRef expected_ref = signature_param_type_ref(signature, i);
        if (!native_reference_binding_allowed(expected_ref, args[i], got_ref, template_params)) {
            return std::nullopt;
        }
        const TypeRef binding_got =
            native_template_binding_arg(expected_ref, got_ref, args[i], template_params);
        const TypeRef& binding_pattern = native_forwarding_reference(expected_ref, template_params)
                                             ? expected_ref.children.front()
                                             : expected_ref;
        const bool bound_template =
            has_type_ref(expected_ref) && has_type_ref(got_ref) &&
            bind_native_template_type_ast(scope.symbols, binding_pattern, binding_got,
                                          template_params, bindings, pack_bindings);
        const TypeRef concrete_expected =
            parameter_with_available_defaults(scope.symbols, signature, expected_ref, bindings);
        if (!native_arg_assignable(scope, args[i], got_ref, expected_ref) &&
            !native_numeric_promotion(expected_ref, got_ref) && !bound_template &&
            !native_arg_assignable(scope, args[i], got_ref, concrete_expected) &&
            !native_numeric_promotion(concrete_expected, got_ref)) {
            return std::nullopt;
        }
    }
    if (pack_param_placeholder) {
        const std::string pack_name = *pack_param_placeholder;
        std::vector<TypeRef> types;
        for (size_t i = fixed_params; i < args.size(); ++i) {
            types.push_back(arg_type_refs[i]);
        }
        pack_bindings[pack_name] = std::move(types);
    }
    bind_default_template_args(signature, bindings);
    for (const std::string& param : signature.template_params) {
        if (template_param_pack(param)) {
            pack_bindings.try_emplace(template_param_name(param));
        }
    }
    return MatchedNativeSignature{
        .signature =
            substitute_bound_template_signature(scope.symbols, signature, bindings, pack_bindings),
        .inferred_template_args = ordered_template_args(signature, bindings, pack_bindings)};
}

int native_match_conversion_score(const FunctionScope& scope, const FunctionSignature& signature,
                                  const std::vector<Expr>& args,
                                  const std::vector<TypeRef>& arg_type_refs) {
    int score = 0;
    const size_t count =
        std::min({signature_param_count(signature), args.size(), arg_type_refs.size()});
    for (size_t i = 0; i < count; ++i) {
        TypeRef expected = resolve_alias_ref(scope.symbols, signature_param_type_ref(signature, i));
        TypeRef got = resolve_alias_ref(scope.symbols, arg_type_refs[i]);
        if (expected.kind == TypeKind::Reference) {
            const bool lvalue = native_arg_is_lvalue(args[i], got);
            if (!lvalue && expected.reference_kind == ReferenceKind::Lvalue) {
                ++score;
            }
        }
        while ((expected.kind == TypeKind::Reference || expected.kind == TypeKind::Const ||
                expected.kind == TypeKind::Volatile) &&
               expected.children.size() == 1) {
            expected = expected.children.front();
        }
        while ((got.kind == TypeKind::Reference || got.kind == TypeKind::Const ||
                got.kind == TypeKind::Volatile) &&
               got.children.size() == 1) {
            got = got.children.front();
        }
        if (type_ref_equivalent(normalize_cpp_type_artifacts_ref(expected),
                                normalize_cpp_type_artifacts_ref(got))) {
            continue;
        }
        score += native_numeric_promotion(expected, got) ? 1 : 2;
    }
    return score;
}

int native_match_qualification_score(const FunctionScope& scope, const FunctionSignature& signature,
                                     const std::vector<TypeRef>& arg_type_refs) {
    int score = 0;
    const size_t count = std::min(signature_param_count(signature), arg_type_refs.size());
    for (size_t i = 0; i < count; ++i) {
        const TypeRef expected =
            resolve_alias_ref(scope.symbols, signature_param_type_ref(signature, i));
        const TypeRef got = resolve_alias_ref(scope.symbols, arg_type_refs[i]);
        if (native_referent_is_const(expected) && !native_referent_is_const(got)) {
            ++score;
        }
    }
    return score;
}

bool direct_native_template_placeholder(const TypeRef& type,
                                        const NativeTemplateParameterNames& template_params) {
    const TypeRef* current = &type;
    while ((current->kind == TypeKind::Const || current->kind == TypeKind::Reference ||
            current->kind == TypeKind::Pointer) &&
           current->children.size() == 1) {
        current = &current->children.front();
    }
    return (current->kind == TypeKind::Named || current->kind == TypeKind::Qualified ||
            current->kind == TypeKind::Value) &&
           template_params.contains(type_ref_head_name(*current));
}

int native_template_generality_score(const FunctionSignature& signature) {
    const NativeTemplateParameterNames template_params = native_template_parameters(signature);
    int score = signature.template_params.empty() ? 0 : 1;
    for (size_t i = 0; i < signature_param_count(signature); ++i) {
        if (direct_native_template_placeholder(signature_param_type_ref(signature, i),
                                               template_params)) {
            ++score;
        }
    }
    return score;
}

bool equivalent_matched_signatures(const FunctionSignature& left, const FunctionSignature& right) {
    if (left.variadic != right.variadic ||
        signature_param_count(left) != signature_param_count(right)) {
        return false;
    }
    if (!type_ref_equivalent(normalize_cpp_type_artifacts_ref(signature_return_type_ref(left)),
                             normalize_cpp_type_artifacts_ref(signature_return_type_ref(right)))) {
        return false;
    }
    for (size_t i = 0; i < signature_param_count(left); ++i) {
        if (!type_ref_equivalent(
                normalize_cpp_type_artifacts_ref(signature_param_type_ref(left, i)),
                normalize_cpp_type_artifacts_ref(signature_param_type_ref(right, i)))) {
            return false;
        }
    }
    return true;
}

std::string ambiguous_native_overload_message(
    const std::string& callee, const std::vector<TypeRef>& arg_type_refs,
    const std::vector<std::pair<size_t, FunctionSignature>>& matches) {
    std::ostringstream out;
    out << "ambiguous native overload of " << callee;
    if (!arg_type_refs.empty()) {
        out << " for arguments ";
        for (size_t i = 0; i < arg_type_refs.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << type_ref_text(arg_type_refs[i]);
        }
    }
    for (const auto& [candidate_index, signature] : matches) {
        (void)candidate_index;
        out << "\ncandidate: " << signature_display(callee, signature);
    }
    return out.str();
}

NativeCandidateSelection
select_native_signature_candidates(const FunctionScope& scope,
                                   std::vector<FunctionSignature> candidates,
                                   const std::vector<TypeRef>& explicit_template_args,
                                   const std::vector<Expr>& args, const SourceLocation* location) {
    NativeCandidateSelection result;
    result.mismatches.resize(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].deleted) {
            result.mismatches[i] = "function is deleted";
            continue;
        }
        if (!explicit_template_args.empty()) {
            result.mismatches[i] =
                explicit_template_mismatch(scope.symbols, candidates[i], explicit_template_args);
            if (!result.mismatches[i]) {
                candidates[i] = substitute_explicit_template_signature(
                    scope.symbols, std::move(candidates[i]), explicit_template_args);
            }
        }
    }
    result.arg_type_refs.reserve(args.size());
    for (const Expr& arg : args) {
        result.arg_type_refs.push_back(infer_expr_type_ast(scope, arg, location));
    }
    std::optional<std::tuple<int, int, int>> selected_rank;
    for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        if (result.mismatches[candidate_index]) {
            continue;
        }
        const FunctionSignature& signature = candidates[candidate_index];
        if (const std::optional<MatchedNativeSignature> matched =
                match_signature_ast(scope, signature, args, result.arg_type_refs)) {
            const int score = native_match_conversion_score(scope, matched->signature, args,
                                                            result.arg_type_refs);
            const int qualification_score =
                native_match_qualification_score(scope, matched->signature, result.arg_type_refs);
            const std::tuple rank{score, native_template_generality_score(signature),
                                  qualification_score};
            if (!result.signature || rank < *selected_rank) {
                result.signature = matched->signature;
                result.inferred_template_args = matched->inferred_template_args;
                result.index = candidate_index;
                selected_rank = rank;
                result.equally_ranked = {{candidate_index, matched->signature}};
            } else if (rank == *selected_rank &&
                       !equivalent_matched_signatures(*result.signature, matched->signature)) {
                result.equally_ranked.emplace_back(candidate_index, matched->signature);
            }
        }
    }
    result.candidates = std::move(candidates);
    return result;
}

FunctionSignature signature_with_implicit_receiver(FunctionSignature signature) {
    signature.param_type_refs.insert(signature.param_type_refs.begin(),
                                     signature.receiver_type_ref);
    if (signature.min_params >= 0) {
        ++signature.min_params;
    }
    if (signature.variadic_param_index >= 0) {
        ++signature.variadic_param_index;
    }
    return signature;
}

FunctionSignature signature_without_implicit_receiver(FunctionSignature signature) {
    if (!signature.param_type_refs.empty()) {
        signature.receiver_type_ref = signature.param_type_refs.front();
        signature.param_type_refs.erase(signature.param_type_refs.begin());
    }
    if (signature.min_params > 0) {
        --signature.min_params;
    }
    if (signature.variadic_param_index > 0) {
        --signature.variadic_param_index;
    }
    return signature;
}

} // namespace dudu::native_match_internal
