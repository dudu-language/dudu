#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_type_internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>

namespace dudu {
namespace {

bool known_structured_template_root(std::string_view name) {
    return name == "list" || name == "span" || name == "strided_span" || name == "strided_span2" ||
           name == "dict" || name == "set" || name == "Option" || name == "Result" ||
           name == "tuple" || name == "variant" || name == "std.function" ||
           name == "std::function";
}

const std::map<std::string, std::string>& builtin_cpp_type_names() {
    static const std::map<std::string, std::string> builtins = {
        {"bool", "bool"},       {"char", "char"},    {"i8", "int8_t"},      {"i16", "int16_t"},
        {"i32", "int32_t"},     {"i64", "int64_t"},  {"u8", "uint8_t"},     {"u16", "uint16_t"},
        {"u32", "uint32_t"},    {"u64", "uint64_t"}, {"isize", "intptr_t"}, {"usize", "size_t"},
        {"f32", "float"},       {"f64", "double"},   {"void", "void"},      {"str", "std::string"},
        {"cstr", "const char*"}, {"slice", "dudu::Slice"}};
    return builtins;
}

std::optional<std::string> lower_parsed_known_template_type(const std::string& type) {
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::Template || !known_structured_template_root(parsed.name)) {
        return std::nullopt;
    }
    return lower_cpp_type(parsed);
}

std::optional<std::string> lower_parsed_template_type(const std::string& type) {
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::Template) {
        return std::nullopt;
    }
    return lower_cpp_type(parsed);
}

std::optional<std::string> lower_parsed_fixed_array_type(const std::string& type) {
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::FixedArray) {
        return std::nullopt;
    }
    return lower_cpp_type(parsed);
}

std::optional<std::string> lower_parsed_wrapper_type(const std::string& type) {
    const TypeRef parsed = parse_type_text(type);
    switch (parsed.kind) {
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Static:
        return lower_cpp_type(parsed);
    default:
        return std::nullopt;
    }
}

} // namespace

std::string lower_cpp_type_name(std::string name) {
    name = trim_copy(std::move(name));
    if (name.empty()) {
        return "void";
    }
    if (name == "None") {
        return "std::monostate";
    }
    if (const auto found = builtin_cpp_type_names().find(name);
        found != builtin_cpp_type_names().end()) {
        return found->second;
    }
    return replace_dots(name);
}

std::string lower_cpp_type_name(std::string name,
                                const std::vector<std::string>& namespace_aliases) {
    return lower_cpp_type_name(strip_c_import_type_aliases(std::move(name), namespace_aliases));
}

std::string lower_cpp_type_name(std::string name, const std::vector<std::string>& namespace_aliases,
                                const CppEmitOptions& options) {
    name = trim_copy(std::move(name));
    const std::string emitted = emitted_type_name(name, options);
    if (emitted != name) {
        return emitted;
    }
    name = strip_c_import_type_aliases(std::move(name), namespace_aliases);
    const std::string emitted_after_alias_strip = emitted_type_name(name, options);
    if (emitted_after_alias_strip != name) {
        return emitted_after_alias_strip;
    }
    return lower_cpp_type_name(std::move(name));
}

std::string strip_c_import_type_aliases(std::string type,
                                        const std::vector<std::string>& namespace_aliases) {
    for (const std::string& alias : namespace_aliases) {
        if (alias.empty() || alias.front() != '!') {
            continue;
        }
        const std::string name = alias.substr(1);
        if (name.empty()) {
            continue;
        }
        const std::string marker = name + ".";
        size_t pos = type.find(marker);
        while (pos != std::string::npos) {
            const bool left_ok =
                pos == 0 || (std::isalnum(static_cast<unsigned char>(type[pos - 1])) == 0 &&
                             type[pos - 1] != '_');
            if (left_ok) {
                type.erase(pos, marker.size());
            } else {
                pos += marker.size();
            }
            pos = type.find(marker, pos);
        }
        if (name.find('.') != std::string::npos) {
            pos = type.find(name);
            while (pos != std::string::npos) {
                const bool left_ok =
                    pos == 0 || (std::isalnum(static_cast<unsigned char>(type[pos - 1])) == 0 &&
                                 type[pos - 1] != '_');
                const size_t after = pos + name.size();
                const bool right_ok = after >= type.size() ||
                                      (std::isalnum(static_cast<unsigned char>(type[after])) == 0 &&
                                       type[after] != '_' && type[after] != '.');
                if (left_ok && right_ok) {
                    const size_t dot = name.rfind('.');
                    const std::string replacement = name.substr(dot + 1);
                    type.replace(pos, name.size(), replacement);
                    pos = type.find(name, pos + replacement.size());
                } else {
                    pos = type.find(name, pos + name.size());
                }
            }
        }
    }
    return type;
}

std::string lower_cpp_type_spelling(const std::string& raw_type) {
    std::string type = trim_copy(raw_type);

    if (type.empty()) {
        return "void";
    }
    if (type == "None") {
        return "std::monostate";
    }
    if (starts_with(type, "fn(")) {
        return lower_cpp_type(parse_type_text(type));
    }
    if (const auto found = builtin_cpp_type_names().find(type);
        found != builtin_cpp_type_names().end()) {
        return found->second;
    }
    if (const auto wrapped_type = lower_parsed_wrapper_type(type)) {
        return *wrapped_type;
    }

    if (const auto template_type = lower_parsed_known_template_type(type)) {
        return *template_type;
    }

    if (const auto array_type = lower_parsed_fixed_array_type(type)) {
        return *array_type;
    }

    const size_t open = type.find('[');
    if (open != std::string::npos && ends_with(type, "]")) {
        if (const auto template_type = lower_parsed_template_type(type)) {
            return *template_type;
        }
    }
    return lower_cpp_type_name(type);
}

std::string lower_cpp_type_spelling(const std::string& raw_type, const CppEmitOptions& options) {
    const std::string type = trim_copy(raw_type);
    const std::string emitted = emitted_type_name(type, options);
    if (emitted != type) {
        return emitted;
    }
    return lower_cpp_type_spelling(type);
}

std::string lower_cpp_type_spelling(const std::string& raw_type,
                                    const std::vector<std::string>& namespace_aliases) {
    return lower_cpp_type_spelling(strip_c_import_type_aliases(raw_type, namespace_aliases));
}

std::string lower_cpp_type_spelling(const std::string& raw_type,
                                    const std::vector<std::string>& namespace_aliases,
                                    const CppEmitOptions& options) {
    const std::string type = trim_copy(raw_type);
    const std::string emitted = emitted_type_name(type, options);
    if (emitted != type) {
        return emitted;
    }
    return lower_cpp_type_spelling(strip_c_import_type_aliases(type, namespace_aliases), options);
}

} // namespace dudu
