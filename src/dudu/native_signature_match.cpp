#include "dudu/native_signature_match.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_signature_templates.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/source.hpp"
#include "dudu/type_compat.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace dudu {
namespace {

using PackBindingMap = std::map<std::string, std::vector<std::string>>;

struct NativeArgType {
    TypeRef ref;
    std::string text;
};

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool arity_matches(const FunctionSignature& signature, size_t arg_count) {
    const size_t min_params = signature.min_params < 0 ? signature.params.size()
                                                       : static_cast<size_t>(signature.min_params);
    return signature.variadic ? arg_count >= min_params
                              : arg_count >= min_params && arg_count <= signature.params.size();
}

bool native_numeric_promotion(const std::string& expected, const std::string& got) {
    return expected == "f64" && got == "f32";
}

bool native_numeric_promotion(const TypeRef& expected, const TypeRef& got) {
    if (!has_type_ref(expected) || !has_type_ref(got)) {
        return false;
    }
    return type_ref_head_name(expected) == "f64" && type_ref_head_name(got) == "f32";
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

std::vector<std::string> native_placeholders_in(std::string_view text) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    size_t pos = 0;
    while (pos < text.size()) {
        if (std::isalnum(static_cast<unsigned char>(text[pos])) == 0 && text[pos] != '_') {
            ++pos;
            continue;
        }
        const size_t start = pos;
        while (pos < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[pos])) != 0 || text[pos] == '_')) {
            ++pos;
        }
        std::string name(text.substr(start, pos - start));
        if (native_template_placeholder(name) && seen.insert(name).second) {
            out.push_back(std::move(name));
        }
    }
    return out;
}

bool native_index_placeholder(const std::string& name) {
    return name == "__i" || name == "__j" || name == "_Int" || name == "_Index" || name == "_Nm";
}

void append_placeholders(std::vector<std::string>& out, std::set<std::string>& seen,
                         const std::vector<std::string>& names, bool only_index) {
    for (std::string name : names) {
        if (native_index_placeholder(name) != only_index) {
            continue;
        }
        if (seen.insert(name).second) {
            out.push_back(std::move(name));
        }
    }
}

std::vector<std::string> native_signature_placeholders(const FunctionSignature& signature) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    append_placeholders(out, seen, native_placeholders_in(signature.return_type), true);
    append_placeholders(out, seen, native_placeholders_in(signature.return_type), false);
    for (const std::string& text : signature.params) {
        append_placeholders(out, seen, native_placeholders_in(text), false);
    }
    return out;
}

std::string replace_explicit_template_args(std::string type, const std::vector<std::string>& names,
                                           const std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size() && i < names.size(); ++i) {
        type = replace_type_identifier(std::move(type), names[i], args[i]);
    }
    return type;
}

void refresh_signature_type_refs(FunctionSignature& signature) {
    signature.param_type_refs.clear();
    signature.param_type_refs.reserve(signature.params.size());
    for (const std::string& param : signature.params) {
        signature.param_type_refs.push_back(parse_type_text(param));
    }
    signature.return_type_ref =
        signature.return_type.empty() ? TypeRef{} : parse_type_text(signature.return_type);
}

FunctionSignature substitute_template_signature(FunctionSignature signature,
                                                const std::vector<std::string>& args) {
    const std::vector<std::string> names = signature.template_params.empty()
                                               ? native_signature_placeholders(signature)
                                               : signature.template_params;
    for (std::string& param : signature.params) {
        param = replace_explicit_template_args(std::move(param), names, args);
    }
    signature.return_type =
        replace_explicit_template_args(std::move(signature.return_type), names, args);
    refresh_signature_type_refs(signature);
    return signature;
}

std::string join_types(const std::vector<std::string>& types) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << types[i];
    }
    return out.str();
}

std::string replace_all(std::string text, const std::string& needle, const std::string& value) {
    size_t pos = text.find(needle);
    while (pos != std::string::npos) {
        text.replace(pos, needle.size(), value);
        pos = text.find(needle, pos + value.size());
    }
    return text;
}

std::string replace_pack_binding(std::string type, const std::string& name,
                                 const std::vector<std::string>& args) {
    const std::string joined = join_types(args);
    type =
        replace_all(std::move(type), "typename __decay_and_strip<" + name + ">::__type...", joined);
    type =
        replace_all(std::move(type), "typename __decay_and_strip<" + name + ">.__type...", joined);
    type = replace_all(std::move(type), name + "...", joined);
    return type;
}

std::string replace_template_bindings(std::string type, const NativeTemplateBindings& bindings,
                                      const PackBindingMap& pack_bindings) {
    for (const auto& [name, args] : pack_bindings) {
        type = replace_pack_binding(std::move(type), name, args);
    }
    for (const auto& [name, arg] : bindings) {
        type = replace_type_identifier(std::move(type), name, arg);
    }
    return type;
}

FunctionSignature substitute_template_signature(FunctionSignature signature,
                                                const NativeTemplateBindings& bindings,
                                                const PackBindingMap& pack_bindings) {
    for (std::string& param : signature.params) {
        param = replace_template_bindings(std::move(param), bindings, pack_bindings);
    }
    signature.return_type =
        replace_template_bindings(std::move(signature.return_type), bindings, pack_bindings);
    refresh_signature_type_refs(signature);
    return signature;
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
    if (signature.min_params >= 0 &&
        static_cast<size_t>(signature.min_params) < signature.params.size()) {
        out << "; min " << signature.min_params;
    }
    out << ") -> " << signature.return_type;
    return out.str();
}

TypeRef signature_param_ref(const FunctionSignature& signature, size_t index) {
    if (index < signature.param_type_refs.size() &&
        has_type_ref(signature.param_type_refs[index])) {
        return signature.param_type_refs[index];
    }
    return index < signature.params.size() ? parse_type_text(signature.params[index]) : TypeRef{};
}

std::string signature_param_text(const FunctionSignature& signature, size_t index) {
    const TypeRef ref = signature_param_ref(signature, index);
    if (has_type_ref(ref)) {
        return substitute_type_ref_text(ref, {});
    }
    return index < signature.params.size() ? signature.params[index] : "";
}

NativeArgType native_arg_type(const FunctionScope& scope, const Expr& arg,
                              const SourceLocation* location,
                              const NativeInferExprTypeAstFn& infer_expr_type) {
    NativeArgType out;
    out.ref = infer_expr_type(scope, arg, location);
    out.text = substitute_type_ref_text(out.ref, {});
    return out;
}

bool native_arg_assignable(const FunctionSignature& signature, size_t index, const Expr& arg,
                           const NativeArgType& got, const NativeCanAssignAstFn& can_assign) {
    const TypeRef expected_ref = signature_param_ref(signature, index);
    if (has_type_ref(expected_ref) && has_type_ref(got.ref) &&
        type_assignment_allowed(expected_ref, got.ref)) {
        return true;
    }
    return can_assign(signature_param_text(signature, index), arg, got.text);
}

std::optional<std::string>
indexed_tuple_return_type(std::string return_type, const std::vector<std::string>& template_args,
                          const std::vector<Expr>& args, const FunctionScope& scope,
                          const SourceLocation* location,
                          const NativeInferExprTypeAstFn& infer_expr_type) {
    return_type = trim(std::move(return_type));
    const bool reference = starts_with(return_type, "&");
    const std::string index_text = reference ? trim(return_type.substr(1)) : return_type;
    if (template_args.empty() || index_text != template_args.front() || args.empty() ||
        index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    const NativeArgType arg_type = native_arg_type(scope, args.front(), location, infer_expr_type);
    const TypeRef tuple =
        has_type_ref(arg_type.ref) ? arg_type.ref : parse_type_text(arg_type.text);
    if (tuple.kind != TypeKind::Template ||
        (tuple.name != "tuple" && tuple.name != "std.tuple" && tuple.name != "std::tuple")) {
        return std::nullopt;
    }
    const size_t index = static_cast<size_t>(std::stoull(index_text));
    if (index >= tuple.children.size()) {
        return std::nullopt;
    }
    const std::string type = substitute_type_ref_text(tuple.children[index], {});
    return reference ? "&" + type : type;
}

std::string mismatch_reason_ast(const FunctionScope& scope, const FunctionSignature& signature,
                                const std::vector<Expr>& args, const SourceLocation* location,
                                const NativeInferExprTypeAstFn& infer_expr_type,
                                const NativeCanAssignAstFn& can_assign) {
    if (!arity_matches(signature, args.size())) {
        std::ostringstream out;
        out << "arity expects ";
        if (signature.variadic) {
            const size_t min_params = signature.min_params < 0
                                          ? signature.params.size()
                                          : static_cast<size_t>(signature.min_params);
            out << "at least " << min_params;
        } else if (signature.min_params >= 0 &&
                   static_cast<size_t>(signature.min_params) < signature.params.size()) {
            out << signature.min_params << " to " << signature.params.size();
        } else {
            out << signature.params.size();
        }
        out << " arguments, got " << args.size();
        return out.str();
    }

    const size_t fixed_params = std::min(signature.params.size(), args.size());
    for (size_t i = 0; i < fixed_params; ++i) {
        const NativeArgType got = native_arg_type(scope, args[i], location, infer_expr_type);
        const TypeRef expected_ref = signature_param_ref(signature, i);
        if (!native_arg_assignable(signature, i, args[i], got, can_assign) &&
            !native_numeric_promotion(expected_ref, got.ref) &&
            !native_numeric_promotion(signature_param_text(signature, i), got.text)) {
            std::ostringstream out;
            out << "parameter " << (i + 1) << " expects " << signature_param_text(signature, i)
                << ", got " << got.text;
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
            out << native_arg_type(scope, args[i], location, infer_expr_type).text;
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
    PackBindingMap pack_bindings;
    const bool has_pack_param =
        signature.variadic && !signature.params.empty() &&
        native_template_pack_placeholder(signature.params.back()).has_value();
    FunctionSignature arity_signature = signature;
    if (has_pack_param &&
        arity_signature.min_params >= static_cast<int>(arity_signature.params.size())) {
        --arity_signature.min_params;
    }
    if (!arity_matches(arity_signature, args.size())) {
        return std::nullopt;
    }
    const size_t fixed_params =
        has_pack_param ? signature.params.size() - 1 : signature.params.size();
    const size_t provided_fixed = std::min(fixed_params, args.size());
    for (size_t i = 0; i < provided_fixed; ++i) {
        const NativeArgType got = native_arg_type(scope, args[i], location, infer_expr_type);
        const TypeRef expected_ref = signature_param_ref(signature, i);
        if (!native_arg_assignable(signature, i, args[i], got, can_assign) &&
            !native_numeric_promotion(expected_ref, got.ref) &&
            !native_numeric_promotion(signature_param_text(signature, i), got.text) &&
            !(has_type_ref(expected_ref) && has_type_ref(got.ref) &&
              bind_native_template_type_ast(expected_ref, got.ref, bindings)) &&
            !bind_native_template_type_ast(scope.symbols, signature_param_text(signature, i),
                                           got.text, bindings) &&
            !bind_native_template_type(signature_param_text(signature, i), got.text, bindings)) {
            return std::nullopt;
        }
    }
    if (has_pack_param) {
        const std::string pack_name = *native_template_pack_placeholder(signature.params.back());
        std::vector<std::string> types;
        for (size_t i = fixed_params; i < args.size(); ++i) {
            types.push_back(native_arg_type(scope, args[i], location, infer_expr_type).text);
        }
        pack_bindings[pack_name] = std::move(types);
    }
    return substitute_template_signature(signature, bindings, pack_bindings);
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

std::optional<std::pair<std::string, std::vector<std::string>>>
native_template_call_base(const std::string& callee) {
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
            signature = substitute_template_signature(std::move(signature), template_call->second);
        }
    }
    for (const FunctionSignature& signature : candidates) {
        if (const std::optional<FunctionSignature> matched = match_signature_ast(
                scope, signature, args, location, infer_expr_type, can_assign)) {
            FunctionSignature resolved = *matched;
            if (template_call) {
                if (const auto indexed =
                        indexed_tuple_return_type(resolved.return_type, template_call->second, args,
                                                  scope, location, infer_expr_type)) {
                    resolved.return_type = *indexed;
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
        signature.return_type = "auto";
        return signature;
    }
    if (location != nullptr) {
        fail(*location, native_overload_message_ast(scope, callee, args, candidates, location,
                                                    infer_expr_type, can_assign));
    }
    return std::nullopt;
}

} // namespace dudu
