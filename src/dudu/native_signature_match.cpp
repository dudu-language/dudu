#include "dudu/native_signature_match.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_signature_substitution.hpp"
#include "dudu/native_signature_templates.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/source.hpp"
#include "dudu/type_compat.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace dudu {
namespace {

struct NativeArgType {
    TypeRef ref;
};

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool arity_matches(const FunctionSignature& signature, size_t arg_count) {
    const size_t param_count = signature_param_count(signature);
    const size_t min_params =
        signature.min_params < 0 ? param_count : static_cast<size_t>(signature.min_params);
    return signature.variadic ? arg_count >= min_params
                              : arg_count >= min_params && arg_count <= param_count;
}

bool native_numeric_promotion(const TypeRef& expected, const TypeRef& got) {
    if (!has_type_ref(expected) || !has_type_ref(got)) {
        return false;
    }
    return type_ref_head_name(expected) == "f64" && type_ref_head_name(got) == "f32";
}

TypeRef signature_param_ref(const FunctionSignature& signature, size_t index) {
    return signature_param_type_ref(signature, index);
}

std::string signature_param_text(const FunctionSignature& signature, size_t index) {
    return signature_param_type_text(signature, index);
}

std::string signature_text(const std::string& callee, const FunctionSignature& signature) {
    std::ostringstream out;
    out << callee << "(";
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        if (i > 0)
            out << ", ";
        out << signature_param_text(signature, i);
    }
    if (signature.variadic) {
        if (param_count != 0)
            out << ", ";
        out << "...";
    }
    if (signature.min_params >= 0 && static_cast<size_t>(signature.min_params) < param_count) {
        out << "; min " << signature.min_params;
    }
    out << ") -> " << signature_return_type_text(signature);
    return out.str();
}

NativeArgType native_arg_type(const FunctionScope& scope, const Expr& arg,
                              const SourceLocation* location,
                              const NativeInferExprTypeAstFn& infer_expr_type) {
    NativeArgType out;
    out.ref = infer_expr_type(scope, arg, location);
    return out;
}

bool native_arg_assignable(const FunctionSignature& signature, size_t index, const Expr& arg,
                           const NativeArgType& got, const NativeCanAssignAstFn& can_assign) {
    const TypeRef expected_ref = signature_param_ref(signature, index);
    if (!has_type_ref(expected_ref) || !has_type_ref(got.ref)) {
        return false;
    }
    if (type_assignment_allowed(expected_ref, got.ref)) {
        return true;
    }
    return can_assign(expected_ref, arg, got.ref);
}

std::optional<TypeRef> indexed_tuple_return_type(const TypeRef& return_type,
                                                 const std::vector<std::string>& template_args,
                                                 const std::vector<Expr>& args,
                                                 const FunctionScope& scope,
                                                 const SourceLocation* location,
                                                 const NativeInferExprTypeAstFn& infer_expr_type) {
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
    if (template_args.empty() || index_text != template_args.front() || args.empty() ||
        index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    const NativeArgType arg_type = native_arg_type(scope, args.front(), location, infer_expr_type);
    const TypeRef tuple = arg_type.ref;
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
                                                const std::vector<std::string>& template_args) {
    if (!has_native_placeholder_ref(return_type)) {
        return std::nullopt;
    }
    const auto first_type_arg = std::ranges::find_if_not(template_args, numeric_template_arg);
    std::vector<std::string> placeholders;
    std::set<std::string> seen;
    collect_native_return_placeholders(return_type, placeholders, seen);
    if (placeholders.empty()) {
        return first_type_arg == template_args.end()
                   ? std::nullopt
                   : std::optional<TypeRef>{
                         native_template_binding_type_ref(*first_type_arg, return_type.location)};
    }
    std::map<std::string, TypeRef> bindings;
    size_t placeholder_index = 0;
    for (const std::string& arg : template_args) {
        if (numeric_template_arg(arg)) {
            continue;
        }
        if (placeholder_index >= placeholders.size()) {
            break;
        }
        bindings.emplace(placeholders[placeholder_index],
                         native_template_binding_type_ref(arg, return_type.location));
        ++placeholder_index;
    }
    if (bindings.empty()) {
        return first_type_arg == template_args.end()
                   ? std::nullopt
                   : std::optional<TypeRef>{
                         native_template_binding_type_ref(*first_type_arg, return_type.location)};
    }
    TypeRef substituted = substitute_type_ref(return_type, bindings);
    return substituted;
}

std::string mismatch_reason_ast(const FunctionScope& scope, const FunctionSignature& signature,
                                const std::vector<Expr>& args, const SourceLocation* location,
                                const NativeInferExprTypeAstFn& infer_expr_type,
                                const NativeCanAssignAstFn& can_assign) {
    if (!arity_matches(signature, args.size())) {
        std::ostringstream out;
        out << "arity expects ";
        const size_t param_count = signature_param_count(signature);
        if (signature.variadic) {
            const size_t min_params =
                signature.min_params < 0 ? param_count : static_cast<size_t>(signature.min_params);
            out << "at least " << min_params;
        } else if (signature.min_params >= 0 &&
                   static_cast<size_t>(signature.min_params) < param_count) {
            out << signature.min_params << " to " << param_count;
        } else {
            out << param_count;
        }
        out << " arguments, got " << args.size();
        return out.str();
    }

    const size_t fixed_params = std::min(signature_param_count(signature), args.size());
    for (size_t i = 0; i < fixed_params; ++i) {
        const NativeArgType got = native_arg_type(scope, args[i], location, infer_expr_type);
        const TypeRef expected_ref = signature_param_ref(signature, i);
        const std::string got_text = substitute_type_ref_text(got.ref, {});
        if (!native_arg_assignable(signature, i, args[i], got, can_assign) &&
            !native_numeric_promotion(expected_ref, got.ref)) {
            std::ostringstream out;
            out << "parameter " << (i + 1) << " expects " << signature_param_text(signature, i)
                << ", got " << got_text;
            return out.str();
        }
    }
    return {};
}

std::string native_overload_message_ast(const FunctionScope& scope, const std::string& callee,
                                        const std::vector<Expr>& args,
                                        const std::vector<FunctionSignature>& candidates,
                                        const SourceLocation* location,
                                        const NativeInferExprTypeAstFn& infer_expr_type,
                                        const NativeCanAssignAstFn& can_assign) {
    std::ostringstream out;
    out << "no native overload of " << callee << " accepts " << args.size() << " arguments";
    if (!args.empty()) {
        out << "\narguments: ";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0)
                out << ", ";
            out << substitute_type_ref_text(
                native_arg_type(scope, args[i], location, infer_expr_type).ref, {});
        }
    }
    for (const FunctionSignature& candidate : candidates) {
        out << "\ncandidate: " << signature_text(callee, candidate);
        if (const std::string reason =
                mismatch_reason_ast(scope, candidate, args, location, infer_expr_type, can_assign);
            !reason.empty()) {
            out << "\n  reason: " << reason;
        }
    }
    return out.str();
}

std::optional<FunctionSignature>
match_signature_ast(const FunctionScope& scope, const FunctionSignature& signature,
                    const std::vector<Expr>& args, const SourceLocation* location,
                    const NativeInferExprTypeAstFn& infer_expr_type,
                    const NativeCanAssignAstFn& can_assign) {
    NativeTemplateBindings bindings;
    NativePackBindingMap pack_bindings;
    const size_t param_count = signature_param_count(signature);
    const TypeRef pack_param_ref =
        param_count == 0 ? TypeRef{} : signature_param_type_ref(signature, param_count - 1);
    const std::optional<std::string> pack_param_placeholder =
        signature.variadic && param_count != 0 ? native_template_pack_placeholder(pack_param_ref)
                                               : std::nullopt;
    const bool has_pack_param = pack_param_placeholder.has_value();
    FunctionSignature arity_signature = signature;
    if (has_pack_param &&
        arity_signature.min_params >= static_cast<int>(signature_param_count(arity_signature))) {
        --arity_signature.min_params;
    }
    if (!arity_matches(arity_signature, args.size())) {
        return std::nullopt;
    }
    const size_t fixed_params =
        has_pack_param ? signature_param_count(signature) - 1 : signature_param_count(signature);
    const size_t provided_fixed = std::min(fixed_params, args.size());
    for (size_t i = 0; i < provided_fixed; ++i) {
        const NativeArgType got = native_arg_type(scope, args[i], location, infer_expr_type);
        const TypeRef expected_ref = signature_param_ref(signature, i);
        if (!native_arg_assignable(signature, i, args[i], got, can_assign) &&
            !native_numeric_promotion(expected_ref, got.ref) &&
            !(has_type_ref(expected_ref) && has_type_ref(got.ref) &&
              bind_native_template_type_ast(scope.symbols, expected_ref, got.ref, bindings))) {
            return std::nullopt;
        }
    }
    if (has_pack_param) {
        const std::string pack_name = *pack_param_placeholder;
        std::vector<TypeRef> types;
        for (size_t i = fixed_params; i < args.size(); ++i) {
            types.push_back(native_arg_type(scope, args[i], location, infer_expr_type).ref);
        }
        pack_bindings[pack_name] = std::move(types);
    }
    return substitute_bound_template_signature(signature, bindings, pack_bindings);
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

std::optional<FunctionSignature>
match_native_signature(const FunctionScope& scope, const std::string& callee,
                       const std::vector<Expr>& args, const SourceLocation* location,
                       const NativeInferExprTypeAstFn& infer_expr_type,
                       const NativeCanAssignAstFn& can_assign) {
    const auto template_call = native_template_call_base(callee);
    const std::string lookup = template_call ? template_call->first : callee;
    const auto found = scope.symbols.native_function_signatures.find(lookup);
    if (found == scope.symbols.native_function_signatures.end()) {
        return std::nullopt;
    }
    std::vector<FunctionSignature> candidates = found->second;
    if (template_call) {
        for (FunctionSignature& signature : candidates) {
            signature =
                substitute_explicit_template_signature(std::move(signature), template_call->second);
        }
    }
    for (const FunctionSignature& signature : candidates) {
        if (const std::optional<FunctionSignature> matched = match_signature_ast(
                scope, signature, args, location, infer_expr_type, can_assign)) {
            FunctionSignature resolved = *matched;
            if (template_call) {
                if (const auto indexed = indexed_tuple_return_type(
                        signature_return_type_ref(resolved), template_call->second, args, scope,
                        location, infer_expr_type)) {
                    set_signature_return_type(resolved, *indexed);
                } else if (const auto explicit_return = explicit_type_return_ref(
                               signature_return_type_ref(resolved), template_call->second)) {
                    set_signature_return_type(resolved, *explicit_return);
                }
            }
            return resolved;
        }
    }
    bool has_variadic_candidate = false;
    for (const FunctionSignature& signature : candidates) {
        has_variadic_candidate = has_variadic_candidate || signature.variadic;
    }
    if (!has_variadic_candidate &&
        explicit_native_template_allowed(scope, lookup, template_call.has_value())) {
        for (const Expr& arg : args) {
            (void)infer_expr_type(scope, arg, location);
        }
        FunctionSignature signature;
        set_signature_return_type(
            signature, named_type_ref("auto", location == nullptr ? SourceLocation{} : *location));
        return signature;
    }
    if (location != nullptr) {
        fail(*location, native_overload_message_ast(scope, callee, args, candidates, location,
                                                    infer_expr_type, can_assign));
    }
    return std::nullopt;
}

} // namespace dudu
