#include "dudu/native_signature_substitution.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_function_type.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace dudu {
namespace {

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

void append_placeholders_from_text(std::vector<std::string>& out, std::set<std::string>& seen,
                                   std::string_view text, bool only_index) {
    append_placeholders(out, seen, native_placeholders_in(text), only_index);
}

void append_placeholder(std::vector<std::string>& out, std::set<std::string>& seen,
                        const std::string& name, bool only_index) {
    if (!native_template_placeholder(name) || native_index_placeholder(name) != only_index) {
        return;
    }
    if (seen.insert(name).second) {
        out.push_back(name);
    }
}

void append_placeholders(std::vector<std::string>& out, std::set<std::string>& seen,
                         const TypeRef& type, bool only_index) {
    if (!has_type_ref(type)) {
        return;
    }
    append_placeholder(out, seen, type_ref_head_name(type), only_index);
    if (!type.value.empty()) {
        append_placeholder(out, seen, type.value, only_index);
    }
    if (type.kind == TypeKind::Unknown && !type.text.empty()) {
        append_placeholders_from_text(out, seen, type.text, only_index);
    }
    for (const TypeRef& child : type.children) {
        append_placeholders(out, seen, child, only_index);
    }
}

std::vector<std::string> native_signature_placeholders(const FunctionSignature& signature) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    append_placeholders(out, seen, signature_return_type_ref(signature), true);
    append_placeholders(out, seen, signature_return_type_ref(signature), false);
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        append_placeholders(out, seen, signature_param_type_ref(signature, i), false);
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

std::string explicit_template_arg_text(const TypeRef& arg) {
    return substitute_type_ref_text(arg, {});
}

std::vector<std::string> explicit_template_arg_texts(const std::vector<TypeRef>& args) {
    std::vector<std::string> out;
    out.reserve(args.size());
    for (const TypeRef& arg : args) {
        out.push_back(explicit_template_arg_text(arg));
    }
    return out;
}

std::map<std::string, TypeRef>
explicit_template_type_ref_bindings(const std::vector<std::string>& names,
                                    const std::vector<TypeRef>& args) {
    std::map<std::string, TypeRef> out;
    size_t arg_index = 0;
    for (const std::string& name : names) {
        if (arg_index >= args.size()) {
            break;
        }
        const std::string arg_text = explicit_template_arg_text(args[arg_index]);
        if (native_index_placeholder(name) && !numeric_template_arg(arg_text)) {
            continue;
        }
        out.emplace(name, args[arg_index]);
        ++arg_index;
    }
    return out;
}

std::vector<TypeRef> explicit_template_type_refs(const std::vector<std::string>& args) {
    std::vector<TypeRef> out;
    out.reserve(args.size());
    for (const std::string& arg : args) {
        out.push_back(native_template_binding_type_ref(arg));
    }
    return out;
}

std::string join_type_refs(const std::vector<TypeRef>& types) {
    std::string out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += substitute_type_ref_text(types[i], {});
    }
    return out;
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
                                 const std::vector<TypeRef>& args) {
    const std::string joined = join_type_refs(args);
    type =
        replace_all(std::move(type), "typename __decay_and_strip<" + name + ">::__type...", joined);
    type =
        replace_all(std::move(type), "typename __decay_and_strip<" + name + ">.__type...", joined);
    type = replace_all(std::move(type), name + "...", joined);
    return type;
}

std::optional<size_t> matching_angle(const std::string& text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '<') {
            ++depth;
        } else if (text[i] == '>') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::nullopt;
}

std::string collapse_decay_and_strip(std::string type) {
    constexpr std::string_view marker = "typename __decay_and_strip<";
    size_t pos = type.find(marker);
    while (pos != std::string::npos) {
        const size_t inner_start = pos + marker.size();
        const std::optional<size_t> close = matching_angle(type, inner_start - 1);
        if (!close) {
            break;
        }
        const std::string dotted_suffix = ".__type";
        const std::string scoped_suffix = "::__type";
        const size_t suffix_start = *close + 1;
        size_t suffix_size = 0;
        if (type.compare(suffix_start, dotted_suffix.size(), dotted_suffix) == 0) {
            suffix_size = dotted_suffix.size();
        } else if (type.compare(suffix_start, scoped_suffix.size(), scoped_suffix) == 0) {
            suffix_size = scoped_suffix.size();
        } else {
            pos = type.find(marker, suffix_start);
            continue;
        }
        const std::string inner = type.substr(inner_start, *close - inner_start);
        type.replace(pos, suffix_start + suffix_size - pos, inner);
        pos = type.find(marker, pos + inner.size());
    }
    return type;
}

std::string replace_template_bindings(std::string type, const NativeTemplateBindings& bindings,
                                      const NativePackBindingMap& pack_bindings) {
    for (const auto& [name, args] : pack_bindings) {
        type = replace_pack_binding(std::move(type), name, args);
    }
    for (const auto& [name, arg] : bindings) {
        type = replace_type_identifier(std::move(type), name, substitute_type_ref_text(arg, {}));
    }
    return collapse_decay_and_strip(std::move(type));
}

std::map<std::string, TypeRef> type_ref_bindings(const NativeTemplateBindings& bindings) {
    return bindings;
}

bool structured_binding_text(std::string_view type) {
    const std::string trimmed = trim_copy(std::string(type));
    return trimmed != "." && trimmed.find(".,") == std::string::npos &&
           trimmed.find(", .") == std::string::npos && trimmed.find("...") == std::string::npos &&
           trimmed.find("__decay_and_strip") == std::string::npos;
}

bool structured_binding_type_ref(const TypeRef& type) {
    if (!has_type_ref(type)) {
        return false;
    }
    if (type.kind == TypeKind::Unknown) {
        return structured_binding_text(type.text);
    }
    for (const TypeRef& child : type.children) {
        if (!structured_binding_type_ref(child)) {
            return false;
        }
    }
    return true;
}

bool structured_binding_texts(const NativeTemplateBindings& bindings) {
    for (const auto& [name, type] : bindings) {
        (void)name;
        if (!structured_binding_type_ref(type)) {
            return false;
        }
    }
    return true;
}

bool structured_signature_texts(const FunctionSignature& signature) {
    if (!structured_binding_type_ref(signature_return_type_ref(signature))) {
        return false;
    }
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        if (!structured_binding_type_ref(signature_param_type_ref(signature, i))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool native_index_placeholder(const std::string& name) {
    return name == "__i" || name == "__j" || name == "_Int" || name == "_Index" || name == "_Nm" ||
           name == "_Np";
}

bool numeric_template_arg(std::string_view arg) {
    arg = trim_copy(std::string(arg));
    return !arg.empty() && arg.find_first_not_of("0123456789") == std::string::npos;
}

TypeRef native_template_binding_type_ref(std::string_view text, SourceLocation location) {
    const std::string trimmed = trim_copy(std::string(text));
    if (numeric_template_arg(trimmed)) {
        TypeRef type;
        type.kind = TypeKind::Value;
        type.value = trimmed;
        type.location = location;
        return type;
    }

    const bool name_like =
        !trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' ||
                   c == ':';
        });
    if (name_like) {
        return named_type_ref(trimmed, location);
    }

    return parse_type_text(trimmed, location);
}

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

FunctionSignature substitute_explicit_template_signature(FunctionSignature signature,
                                                         const std::vector<std::string>& args) {
    return substitute_explicit_template_signature(std::move(signature),
                                                  explicit_template_type_refs(args));
}

FunctionSignature substitute_explicit_template_signature(FunctionSignature signature,
                                                         const std::vector<TypeRef>& args) {
    const std::vector<std::string> names = signature.template_params.empty()
                                               ? native_signature_placeholders(signature)
                                               : signature.template_params;
    const std::map<std::string, TypeRef> ref_bindings =
        explicit_template_type_ref_bindings(names, args);
    const std::vector<std::string> arg_texts = explicit_template_arg_texts(args);
    std::vector<TypeRef> param_types;
    param_types.reserve(signature_param_count(signature));
    for (size_t i = 0; i < signature_param_count(signature); ++i) {
        const TypeRef param_type = signature_param_type_ref(signature, i);
        if (has_type_ref(param_type)) {
            param_types.push_back(substitute_type_ref(param_type, ref_bindings));
        } else {
            const std::string param_text = replace_explicit_template_args(
                signature_param_type_text(signature, i), names, arg_texts);
            param_types.push_back(parse_type_text(param_text));
        }
    }
    set_signature_param_types(signature, std::move(param_types));
    const TypeRef return_type = signature_return_type_ref(signature);
    if (has_type_ref(return_type)) {
        set_signature_return_type(signature, substitute_type_ref(return_type, ref_bindings));
    } else {
        set_signature_return_type(
            signature, parse_type_text(replace_explicit_template_args(
                                           signature_return_type_text(signature), names, arg_texts),
                                       return_type.location));
    }
    return signature;
}

FunctionSignature substitute_bound_template_signature(FunctionSignature signature,
                                                      const NativeTemplateBindings& bindings,
                                                      const NativePackBindingMap& pack_bindings) {
    if (pack_bindings.empty() && structured_binding_texts(bindings) &&
        structured_signature_texts(signature)) {
        const std::map<std::string, TypeRef> refs = type_ref_bindings(bindings);
        std::vector<TypeRef> params;
        params.reserve(signature_param_count(signature));
        for (size_t i = 0; i < signature_param_count(signature); ++i) {
            const TypeRef param_type = signature_param_type_ref(signature, i);
            params.push_back(substitute_type_ref(param_type, refs));
        }
        set_signature_param_types(signature, std::move(params));
        const TypeRef return_type = signature_return_type_ref(signature);
        set_signature_return_type(signature, substitute_type_ref(return_type, refs));
        return signature;
    }

    std::vector<TypeRef> params;
    params.reserve(signature_param_count(signature));
    for (size_t i = 0; i < signature_param_count(signature); ++i) {
        const TypeRef param_type = signature_param_type_ref(signature, i);
        params.push_back(
            parse_type_text(replace_template_bindings(signature_param_type_text(signature, i),
                                                      bindings, pack_bindings),
                            param_type.location));
    }
    set_signature_param_types(signature, std::move(params));
    const TypeRef return_type = signature_return_type_ref(signature);
    set_signature_return_type(
        signature, parse_type_text(replace_template_bindings(signature_return_type_text(signature),
                                                             bindings, pack_bindings),
                                   return_type.location));
    return signature;
}

} // namespace dudu
