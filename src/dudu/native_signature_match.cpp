#include "dudu/native_signature_match.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace dudu {
namespace {

using BindingMap = std::map<std::string, std::string>;
using PackBindingMap = std::map<std::string, std::vector<std::string>>;

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

bool native_template_placeholder(const std::string& type);

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
    return signature;
}

bool native_template_placeholder(const std::string& type) {
    static const std::set<std::string> simple = {
        "T", "U", "V", "A", "B", "L", "Q", "Key", "Value", "__i", "__j", "_Int", "_Index", "_Nm"};
    if (simple.contains(type)) {
        return true;
    }
    if (type.size() == 1 && std::isupper(static_cast<unsigned char>(type.front())) != 0) {
        return true;
    }
    return type.size() > 1 && type.front() == '_' &&
           std::isupper(static_cast<unsigned char>(type[1])) != 0;
}

bool bind_template_placeholder(const std::string& name, const std::string& got,
                               BindingMap& bindings) {
    const auto found = bindings.find(name);
    if (found == bindings.end()) {
        bindings[name] = got;
        return true;
    }
    return found->second == got;
}

std::optional<std::pair<std::string, std::string>> wrapped_type(std::string type) {
    type = trim(std::move(type));
    const size_t open = type.find('[');
    if (open == std::string::npos || !type.ends_with("]")) {
        return std::nullopt;
    }
    return std::make_pair(trim(type.substr(0, open)),
                          trim(type.substr(open + 1, type.size() - open - 2)));
}

std::string strip_forwarding_suffix(std::string type) {
    type = trim(std::move(type));
    if (type.ends_with("...")) {
        type = trim(type.substr(0, type.size() - 3));
    }
    while (!type.empty() && type.back() == '&') {
        type = trim(type.substr(0, type.size() - 1));
    }
    return type;
}

std::optional<std::string> template_pack_placeholder(std::string type) {
    type = strip_forwarding_suffix(std::move(type));
    if (native_template_placeholder(type)) {
        return type;
    }
    return std::nullopt;
}

bool bind_template_type(std::string expected, std::string got, BindingMap& bindings) {
    expected = trim(std::move(expected));
    got = trim(std::move(got));
    if (native_template_placeholder(expected)) {
        return bind_template_placeholder(expected, got, bindings);
    }
    if (!expected.empty() && expected.front() == '&') {
        if (!got.empty() && got.front() == '&') {
            got = trim(got.substr(1));
        }
        return bind_template_type(expected.substr(1), std::move(got), bindings);
    }
    if (!expected.empty() && expected.front() == '*') {
        if (got.empty() || got.front() != '*') {
            return false;
        }
        return bind_template_type(expected.substr(1), got.substr(1), bindings);
    }
    const std::optional<std::pair<std::string, std::string>> expected_wrap = wrapped_type(expected);
    if (expected_wrap.has_value()) {
        if (const std::optional<std::pair<std::string, std::string>> got_wrap = wrapped_type(got);
            got_wrap.has_value() && got_wrap->first == expected_wrap->first) {
            got = got_wrap->second;
        }
        return bind_template_type(expected_wrap->second, std::move(got), bindings);
    }
    return false;
}

std::string type_ref_binding_text(const TypeRef& type) {
    return substitute_type_ref_text(type, {});
}

bool bind_template_type_ref(const TypeRef& expected, const TypeRef& got, BindingMap& bindings);

bool bind_same_shape_children(const TypeRef& expected, const TypeRef& got, BindingMap& bindings) {
    if (expected.children.size() != got.children.size()) {
        return false;
    }
    for (size_t i = 0; i < expected.children.size(); ++i) {
        if (!bind_template_type_ref(expected.children[i], got.children[i], bindings)) {
            return false;
        }
    }
    return true;
}

bool same_native_template_name(const std::string& expected, const std::string& got) {
    return expected == got || expected.ends_with("." + got) || got.ends_with("." + expected);
}

bool bind_template_type_ref(const TypeRef& expected, const TypeRef& got, BindingMap& bindings) {
    const std::string expected_name =
        expected.name.empty() ? trim_copy(expected.text) : trim_copy(expected.name);
    if ((expected.kind == TypeKind::Named || expected.kind == TypeKind::Qualified ||
         expected.kind == TypeKind::Value) &&
        native_template_placeholder(expected_name.empty() ? trim_copy(expected.value)
                                                          : expected_name)) {
        return bind_template_placeholder(expected_name.empty() ? trim_copy(expected.value)
                                                               : expected_name,
                                         type_ref_binding_text(got), bindings);
    }
    if (expected.kind == TypeKind::Pointer) {
        return got.kind == TypeKind::Pointer && expected.children.size() == 1 &&
               got.children.size() == 1 &&
               bind_template_type_ref(expected.children.front(), got.children.front(), bindings);
    }
    if (expected.kind == TypeKind::Reference) {
        if (expected.children.size() != 1) {
            return false;
        }
        return got.kind == TypeKind::Reference && got.children.size() == 1
                   ? bind_template_type_ref(expected.children.front(), got.children.front(),
                                            bindings)
                   : bind_template_type_ref(expected.children.front(), got, bindings);
    }
    if (expected.kind == TypeKind::Const || expected.kind == TypeKind::Volatile ||
        expected.kind == TypeKind::Atomic || expected.kind == TypeKind::Storage ||
        expected.kind == TypeKind::Shared || expected.kind == TypeKind::Device) {
        if (expected.children.size() != 1) {
            return false;
        }
        return got.kind == expected.kind && got.children.size() == 1
                   ? bind_template_type_ref(expected.children.front(), got.children.front(),
                                            bindings)
                   : bind_template_type_ref(expected.children.front(), got, bindings);
    }
    if (expected.kind == TypeKind::Template && got.kind == TypeKind::Template &&
        same_native_template_name(expected.name, got.name)) {
        return bind_same_shape_children(expected, got, bindings);
    }
    if (expected.kind == TypeKind::FixedArray && got.kind == TypeKind::FixedArray &&
        expected.value == got.value) {
        return bind_same_shape_children(expected, got, bindings);
    }
    return false;
}

bool bind_template_type_ast(const Symbols& symbols, const std::string& expected,
                            const std::string& got, BindingMap& bindings) {
    const TypeRef expected_ref = parse_type_text(expected);
    const TypeRef got_ref = parse_type_text(resolve_alias(symbols, got));
    return bind_template_type_ref(expected_ref, got_ref, bindings);
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

std::string replace_template_bindings(std::string type, const BindingMap& bindings,
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
                                                const BindingMap& bindings,
                                                const PackBindingMap& pack_bindings) {
    for (std::string& param : signature.params) {
        param = replace_template_bindings(std::move(param), bindings, pack_bindings);
    }
    signature.return_type =
        replace_template_bindings(std::move(signature.return_type), bindings, pack_bindings);
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

std::optional<FunctionSignature> match_signature_ast(const FunctionScope& scope,
                                                     const FunctionSignature& signature,
                                                     const std::vector<Expr>& args,
                                                     const SourceLocation* location,
                                                     const NativeInferExprAstFn& infer_expr,
                                                     const NativeCanAssignAstFn& can_assign) {
    BindingMap bindings;
    PackBindingMap pack_bindings;
    const bool has_pack_param = signature.variadic && !signature.params.empty() &&
                                template_pack_placeholder(signature.params.back()).has_value();
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
        const std::string got = infer_expr(scope, args[i], location);
        if (!can_assign(signature.params[i], args[i], got) &&
            !native_numeric_promotion(signature.params[i], got) &&
            !bind_template_type_ast(scope.symbols, signature.params[i], got, bindings) &&
            !bind_template_type(signature.params[i], got, bindings)) {
            return std::nullopt;
        }
    }
    if (has_pack_param) {
        const std::string pack_name = *template_pack_placeholder(signature.params.back());
        std::vector<std::string> types;
        for (size_t i = fixed_params; i < args.size(); ++i) {
            types.push_back(infer_expr(scope, args[i], location));
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

std::optional<FunctionSignature> match_native_signature(const FunctionScope& scope,
                                                        const std::string& callee,
                                                        const std::vector<Expr>& args,
                                                        const SourceLocation* location,
                                                        const NativeInferExprAstFn& infer_expr,
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
        if (const std::optional<FunctionSignature> matched =
                match_signature_ast(scope, signature, args, location, infer_expr, can_assign)) {
            return matched;
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
