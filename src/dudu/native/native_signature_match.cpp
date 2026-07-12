#include "dudu/native/native_signature_match.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/native/native_signature_templates.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool native_variadic_pack_param(const TypeRef& type) {
    if (native_template_pack_placeholder(type)) {
        return true;
    }
    return type.kind == TypeKind::PackExpansion && type.children.empty();
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
    for (size_t i = pack_start; i < signature_param_count(signature); ++i) {
        if (const std::optional<std::string> pack =
                native_template_pack_placeholder(signature_param_type_ref(signature, i))) {
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

bool native_numeric_promotion(const TypeRef& expected, const TypeRef& got) {
    if (!has_type_ref(expected) || !has_type_ref(got)) {
        return false;
    }
    return type_ref_head_name(expected) == "f64" && type_ref_head_name(got) == "f32";
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
    if (native_fixed_array_alias_decay_allowed(scope, expected_ref, got_ref)) {
        return true;
    }
    if (type_assignment_allowed(expected_ref, got_ref)) {
        return true;
    }
    return can_assign_ast(scope, expected_ref, arg, got_ref);
}

std::optional<TypeRef> indexed_tuple_return_type(const TypeRef& return_type,
                                                 const std::vector<TypeRef>& template_args,
                                                 const std::vector<TypeRef>& arg_type_refs) {
    const bool reference = return_type.kind == TypeKind::Reference;
    const TypeRef* index_type = &return_type;
    if (reference) {
        if (return_type.children.size() != 1) {
            return std::nullopt;
        }
        index_type = &return_type.children.front();
    }
    if (index_type->kind != TypeKind::Value) {
        return std::nullopt;
    }
    const std::string index_text = index_type->value;
    if (template_args.empty() || template_args.front().kind != TypeKind::Value ||
        index_text != template_args.front().value || arg_type_refs.empty() || index_text.empty() ||
        index_text.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    const TypeRef& tuple = arg_type_refs.front();
    if (tuple.kind != TypeKind::Template ||
        (tuple.name != "tuple" && tuple.name != "std.tuple" && tuple.name != "std::tuple")) {
        return std::nullopt;
    }
    const size_t index = static_cast<size_t>(std::stoull(index_text));
    if (index >= tuple.children.size()) {
        return std::nullopt;
    }
    if (!reference) {
        return tuple.children[index];
    }
    TypeRef out;
    out.kind = TypeKind::Reference;
    out.children.push_back(tuple.children[index]);
    out.location = return_type.location;
    return out;
}

bool numeric_template_arg_ref(const TypeRef& arg) {
    return arg.kind == TypeKind::Value && numeric_template_arg(arg.value);
}

bool has_native_placeholder_ref(const TypeRef& type) {
    const std::string head = type_ref_head_name(type);
    if (native_template_placeholder(head) ||
        (!type.value.empty() && native_template_placeholder(type.value))) {
        return true;
    }
    for (const TypeRef& child : type.children) {
        if (has_native_placeholder_ref(child)) {
            return true;
        }
    }
    return false;
}

bool contains_declared_template_param(const TypeRef& type,
                                      const std::vector<std::string>& params) {
    const auto is_param = [&](std::string_view candidate) {
        return std::ranges::any_of(params, [&](std::string param) {
            if (param.ends_with("...")) {
                param.resize(param.size() - 3);
            }
            return candidate == param;
        });
    };
    if (is_param(type_ref_head_name(type)) || (!type.value.empty() && is_param(type.value))) {
        return true;
    }
    return std::ranges::any_of(type.children, [&](const TypeRef& child) {
        return contains_declared_template_param(child, params);
    });
}

bool needs_native_return_fallback(const FunctionSignature& signature,
                                  const TypeRef& return_type) {
    if (!signature.has_native_template_return_spelling) {
        return false;
    }
    if (!signature.template_params.empty()) {
        return contains_declared_template_param(return_type, signature.template_params);
    }
    return has_native_placeholder_ref(return_type);
}

void collect_native_return_placeholders(const TypeRef& type, std::vector<std::string>& out,
                                        std::set<std::string>& seen) {
    const std::string head = type_ref_head_name(type);
    if (native_template_placeholder(head) && !native_index_placeholder(head) &&
        seen.insert(head).second) {
        out.push_back(head);
    }
    if (!type.value.empty() && native_template_placeholder(type.value) &&
        !native_index_placeholder(type.value) && seen.insert(type.value).second) {
        out.push_back(type.value);
    }
    for (const TypeRef& child : type.children) {
        collect_native_return_placeholders(child, out, seen);
    }
}

std::optional<TypeRef> explicit_type_return_ref(const TypeRef& return_type,
                                                const std::vector<TypeRef>& template_args) {
    if (!has_native_placeholder_ref(return_type)) {
        return std::nullopt;
    }
    const auto first_type_arg = std::ranges::find_if_not(template_args, numeric_template_arg_ref);
    std::vector<std::string> placeholders;
    std::set<std::string> seen;
    collect_native_return_placeholders(return_type, placeholders, seen);
    if (placeholders.empty()) {
        return first_type_arg == template_args.end() ? std::nullopt
                                                     : std::optional<TypeRef>{*first_type_arg};
    }
    std::map<std::string, TypeRef> bindings;
    size_t placeholder_index = 0;
    for (const TypeRef& arg : template_args) {
        if (numeric_template_arg_ref(arg)) {
            continue;
        }
        if (placeholder_index >= placeholders.size()) {
            break;
        }
        bindings.emplace(placeholders[placeholder_index], arg);
        ++placeholder_index;
    }
    if (bindings.empty()) {
        return first_type_arg == template_args.end() ? std::nullopt
                                                     : std::optional<TypeRef>{*first_type_arg};
    }
    TypeRef substituted = substitute_type_ref(return_type, bindings);
    return substituted;
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
    const size_t fixed_params = std::min(signature_param_count(signature), args.size());
    for (size_t i = 0; i < fixed_params; ++i) {
        const TypeRef& got_ref = arg_type_refs[i];
        const TypeRef expected_ref = signature_param_type_ref(signature, i);
        const std::string got_display = substitute_type_ref_text(got_ref, {});
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

std::string native_overload_message_ast(const FunctionScope& scope, const std::string& callee,
                                        const std::vector<Expr>& args,
                                        const std::vector<TypeRef>& arg_type_refs,
                                        const std::vector<FunctionSignature>& candidates) {
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
    for (const FunctionSignature& candidate : candidates) {
        out << "\ncandidate: " << signature_display(callee, candidate);
        for (const std::string& reason :
             mismatch_reasons_ast(scope, candidate, args, arg_type_refs)) {
            out << "\n  reason: " << reason;
        }
    }
    return out.str();
}

std::optional<FunctionSignature> match_signature_ast(const FunctionScope& scope,
                                                     const FunctionSignature& signature,
                                                     const std::vector<Expr>& args,
                                                     const std::vector<TypeRef>& arg_type_refs) {
    NativeTemplateBindings bindings;
    NativePackBindingMap pack_bindings;
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
        const bool bound_template =
            has_type_ref(expected_ref) && has_type_ref(got_ref) &&
            bind_native_template_type_ast(scope.symbols, expected_ref, got_ref, bindings);
        if (!native_arg_assignable(scope, args[i], got_ref, expected_ref) &&
            !native_numeric_promotion(expected_ref, got_ref) && !bound_template) {
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
    return substitute_bound_template_signature(scope.symbols, signature, bindings, pack_bindings);
}

int native_match_conversion_score(const FunctionScope& scope, const FunctionSignature& signature,
                                  const std::vector<TypeRef>& arg_type_refs) {
    int score = 0;
    const size_t count = std::min(signature_param_count(signature), arg_type_refs.size());
    for (size_t i = 0; i < count; ++i) {
        TypeRef expected =
            resolve_alias_ref(scope.symbols, signature_param_type_ref(signature, i));
        TypeRef got = resolve_alias_ref(scope.symbols, arg_type_refs[i]);
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

bool direct_native_template_placeholder(const TypeRef& type) {
    const TypeRef* current = &type;
    while ((current->kind == TypeKind::Const || current->kind == TypeKind::Reference ||
            current->kind == TypeKind::Pointer) &&
           current->children.size() == 1) {
        current = &current->children.front();
    }
    return (current->kind == TypeKind::Named || current->kind == TypeKind::Qualified ||
            current->kind == TypeKind::Value) &&
           native_template_placeholder(type_ref_head_name(*current));
}

int native_template_generality_score(const FunctionSignature& signature) {
    int score = 0;
    for (size_t i = 0; i < signature_param_count(signature); ++i) {
        if (direct_native_template_placeholder(signature_param_type_ref(signature, i))) {
            ++score;
        }
    }
    return score;
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
    const std::string root = lookup.substr(0, dot);
    return scope.symbols.native_explicit_template_prefixes.contains(root);
}

} // namespace

std::optional<NativeSignatureMatch>
match_native_signature_declaration(const FunctionScope& scope, const std::string& callee,
                                   const std::vector<TypeRef>& explicit_template_args,
                                   const std::vector<Expr>& args, const SourceLocation* location) {
    const std::string& lookup = callee;
    const auto found = scope.symbols.native_function_signatures.find(lookup);
    if (found == scope.symbols.native_function_signatures.end()) {
        return std::nullopt;
    }
    std::vector<FunctionSignature> candidates = found->second;
    if (!explicit_template_args.empty()) {
        for (FunctionSignature& signature : candidates) {
            signature = substitute_explicit_template_signature(scope.symbols, std::move(signature),
                                                               explicit_template_args);
        }
    }
    std::vector<TypeRef> arg_type_refs;
    arg_type_refs.reserve(args.size());
    for (const Expr& arg : args) {
        arg_type_refs.push_back(infer_expr_type_ast(scope, arg, location));
    }
    std::optional<FunctionSignature> selected;
    size_t selected_index = 0;
    int selected_score = 0;
    for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        const FunctionSignature& signature = candidates[candidate_index];
        if (const std::optional<FunctionSignature> matched =
                match_signature_ast(scope, signature, args, arg_type_refs)) {
            const int score = native_match_conversion_score(scope, *matched, arg_type_refs);
            const int ranked_score = score * 100 + native_template_generality_score(signature);
            if (!selected || ranked_score < selected_score) {
                selected = *matched;
                selected_index = candidate_index;
                selected_score = ranked_score;
            }
        }
    }
    if (selected) {
        if (!explicit_template_args.empty()) {
            const FunctionSignature& original = candidates[selected_index];
            if (const auto indexed = indexed_tuple_return_type(
                    signature_return_type_ref(*selected), explicit_template_args, arg_type_refs)) {
                set_signature_return_type(*selected, *indexed);
            } else if (needs_native_return_fallback(original,
                                                    signature_return_type_ref(*selected))) {
                const auto explicit_return = explicit_type_return_ref(
                    signature_return_type_ref(*selected), explicit_template_args);
                if (explicit_return) {
                    set_signature_return_type(*selected, *explicit_return);
                }
            }
        }
        const NativeFunctionDecl* declaration =
            native_function_decl_for_overload(scope.symbols, lookup, selected_index);
        return NativeSignatureMatch{.signature = std::move(*selected), .declaration = declaration};
    }
    bool has_variadic_candidate = false;
    for (const FunctionSignature& signature : candidates) {
        has_variadic_candidate = has_variadic_candidate || signature.variadic;
    }
    if (!has_variadic_candidate &&
        explicit_native_template_allowed(scope, lookup, !explicit_template_args.empty())) {
        FunctionSignature signature;
        set_signature_return_type(
            signature, named_type_ref("auto", location == nullptr ? SourceLocation{} : *location));
        return NativeSignatureMatch{.signature = std::move(signature), .declaration = nullptr};
    }
    if (location != nullptr) {
        fail(*location,
             native_overload_message_ast(scope, callee, args, arg_type_refs, candidates));
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

} // namespace dudu
