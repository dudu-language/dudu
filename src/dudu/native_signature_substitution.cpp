#include "dudu/native_signature_substitution.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_common.hpp"

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
    for (const TypeRef& child : type.children) {
        append_placeholders(out, seen, child, only_index);
    }
}

std::vector<std::string> native_signature_placeholders(const FunctionSignature& signature) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    append_placeholders(out, seen, signature.return_type_ref, true);
    append_placeholders(out, seen, native_placeholders_in(signature.return_type), true);
    append_placeholders(out, seen, signature.return_type_ref, false);
    append_placeholders(out, seen, native_placeholders_in(signature.return_type), false);
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i < signature.param_type_refs.size()) {
            append_placeholders(out, seen, signature.param_type_refs[i], false);
        }
        append_placeholders(out, seen, native_placeholders_in(signature.params[i]), false);
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

std::map<std::string, std::string>
explicit_template_bindings(const std::vector<std::string>& names,
                           const std::vector<std::string>& args) {
    std::map<std::string, std::string> out;
    size_t arg_index = 0;
    for (const std::string& name : names) {
        if (arg_index >= args.size()) {
            break;
        }
        if (native_index_placeholder(name) && !numeric_template_arg(args[arg_index])) {
            continue;
        }
        out.emplace(name, args[arg_index]);
        ++arg_index;
    }
    return out;
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

std::string join_types(const std::vector<std::string>& types) {
    std::string out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += types[i];
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
                                 const std::vector<std::string>& args) {
    const std::string joined = join_types(args);
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
        type = replace_type_identifier(std::move(type), name, arg);
    }
    return collapse_decay_and_strip(std::move(type));
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
    const std::vector<std::string> names = signature.template_params.empty()
                                               ? native_signature_placeholders(signature)
                                               : signature.template_params;
    const std::map<std::string, std::string> bindings = explicit_template_bindings(names, args);
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i < signature.param_type_refs.size() && has_type_ref(signature.param_type_refs[i])) {
            signature.param_type_refs[i] =
                substitute_type_ref(signature.param_type_refs[i], bindings);
            signature.params[i] = substitute_type_ref_text(signature.param_type_refs[i], {});
        } else {
            signature.params[i] =
                replace_explicit_template_args(std::move(signature.params[i]), names, args);
        }
    }
    if (has_type_ref(signature.return_type_ref)) {
        signature.return_type_ref = substitute_type_ref(signature.return_type_ref, bindings);
        signature.return_type = substitute_type_ref_text(signature.return_type_ref, {});
    } else {
        signature.return_type =
            replace_explicit_template_args(std::move(signature.return_type), names, args);
    }
    refresh_signature_type_refs(signature);
    return signature;
}

FunctionSignature substitute_bound_template_signature(FunctionSignature signature,
                                                      const NativeTemplateBindings& bindings,
                                                      const NativePackBindingMap& pack_bindings) {
    for (std::string& param : signature.params) {
        param = replace_template_bindings(std::move(param), bindings, pack_bindings);
    }
    signature.return_type =
        replace_template_bindings(std::move(signature.return_type), bindings, pack_bindings);
    refresh_signature_type_refs(signature);
    return signature;
}

} // namespace dudu
