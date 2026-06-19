#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_type_internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>

namespace dudu {
namespace {

bool is_decimal_number(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

bool is_constant_name(std::string_view text) {
    if (text.empty() || std::isalpha(static_cast<unsigned char>(text.front())) == 0) {
        return false;
    }
    bool has_upper = false;
    for (const char c : text) {
        if (std::isupper(static_cast<unsigned char>(c)) != 0) {
            has_upper = true;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '_') {
            continue;
        }
        return false;
    }
    return has_upper;
}

bool is_array_dimension(std::string_view text) {
    return is_decimal_number(text) || is_constant_name(text);
}

const std::map<std::string, std::string>& builtin_cpp_type_names() {
    static const std::map<std::string, std::string> builtins = {
        {"bool", "bool"},       {"i8", "int8_t"},    {"i16", "int16_t"},
        {"i32", "int32_t"},     {"i64", "int64_t"},  {"u8", "uint8_t"},
        {"u16", "uint16_t"},    {"u32", "uint32_t"}, {"u64", "uint64_t"},
        {"isize", "intptr_t"},  {"usize", "size_t"}, {"f32", "float"},
        {"f64", "double"},      {"void", "void"},    {"str", "std::string"},
        {"cstr", "const char*"}};
    return builtins;
}

std::optional<std::string> lower_parsed_fixed_array_type(const std::string& type) {
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::FixedArray) {
        return std::nullopt;
    }
    return lower_cpp_type(parsed);
}

std::string lower_function_signature_type(const std::string& type, bool pointer) {
    const size_t open = type.find('(');
    const size_t close = type.find(')', open);
    if (open == std::string::npos || close == std::string::npos) {
        return replace_dots(type);
    }
    const std::string args = type.substr(open + 1, close - open - 1);
    std::string result = "void";
    const size_t arrow = type.find("->", close);
    if (arrow != std::string::npos) {
        result = lower_cpp_type(type.substr(arrow + 2));
    }

    std::ostringstream signature;
    signature << result << '(';
    const std::vector<std::string> parts = split_top_level_args(args);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            signature << ", ";
        }
        signature << lower_cpp_type(parts[i]);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_template_arg_type(const std::string& type) {
    const std::string trimmed = trim_copy(type);
    if (starts_with(trimmed, "fn(")) {
        return lower_function_signature_type(trimmed, false);
    }
    return lower_cpp_type(trimmed);
}

std::string lower_template_type(std::string_view name, const std::string& args) {
    if (name == "list") {
        return "std::vector<" + lower_template_arg_type(args) + ">";
    }
    if (name == "span") {
        return "std::span<" + lower_template_arg_type(args) + ">";
    }
    if (name == "strided_span") {
        return "dudu::StridedSpan<" + lower_template_arg_type(args) + ">";
    }
    if (name == "dict") {
        std::ostringstream out;
        out << "std::unordered_map<";
        const std::vector<std::string> parts = split_top_level_args(args);
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_template_arg_type(parts[i]);
        }
        out << ">";
        return out.str();
    }
    if (name == "set") {
        return "std::unordered_set<" + lower_template_arg_type(args) + ">";
    }
    if (name == "Option") {
        return "std::optional<" + lower_template_arg_type(args) + ">";
    }
    if (name == "Result") {
        std::ostringstream out;
        out << "dudu::Result<";
        const std::vector<std::string> parts = split_top_level_args(args);
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_template_arg_type(parts[i]);
        }
        out << ">";
        return out.str();
    }
    if (name == "tuple") {
        std::ostringstream out;
        const std::vector<std::string> parts = split_top_level_args(args);
        out << "dudu::Tuple" << parts.size() << "<";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_template_arg_type(parts[i]);
        }
        out << ">";
        return out.str();
    }
    if (name == "variant") {
        std::ostringstream out;
        out << "std::variant<";
        const std::vector<std::string> parts = split_top_level_args(args);
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_template_arg_type(parts[i]);
        }
        out << ">";
        return out.str();
    }
    if (name == "const") {
        const TypeRef parsed = parse_type_text(args);
        if (parsed.kind == TypeKind::Pointer && !parsed.children.empty()) {
            return lower_cpp_type(parsed.children.front()) + "* const";
        }
        if (parsed.kind == TypeKind::Reference) {
            return lower_cpp_type(parsed);
        }
        return "const " + lower_template_arg_type(args);
    }
    if (name == "atomic") {
        return "std::atomic<" + lower_template_arg_type(args) + ">";
    }
    if (name == "volatile") {
        return "volatile " + lower_template_arg_type(args);
    }
    if (name == "device" || name == "storage" || name == "shared") {
        return lower_template_arg_type(args);
    }
    std::ostringstream out;
    out << replace_dots(std::string(name)) << "<";
    const std::vector<std::string> parts = split_top_level_args(args);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_template_arg_type(parts[i]);
    }
    out << ">";
    return out.str();
}

std::string lower_function_type(const std::string& type) {
    return lower_function_signature_type(type, true);
}

std::vector<std::string> fixed_array_dimensions(const std::string& type) {
    std::vector<std::string> dims;
    size_t open = type.find('[');
    while (open != std::string::npos) {
        const size_t close = type.find(']', open + 1);
        if (close == std::string::npos) {
            return {};
        }
        const std::string dim = type.substr(open + 1, close - open - 1);
        if (!is_array_dimension(dim)) {
            return {};
        }
        dims.push_back(dim);
        open = type.find('[', close + 1);
    }
    return dims;
}

std::string fixed_array_base(const std::string& type) {
    const size_t open = type.find('[');
    return open == std::string::npos ? type : type.substr(0, open);
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

std::string lower_cpp_type_name(std::string name,
                                const std::vector<std::string>& namespace_aliases,
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

std::string lower_cpp_type(const std::string& raw_type) {
    std::string type = trim_copy(raw_type);

    if (type.empty()) {
        return "void";
    }
    if (type == "None") {
        return "std::monostate";
    }
    if (starts_with(type, "fn(")) {
        return lower_function_type(type);
    }
    if (const auto found = builtin_cpp_type_names().find(type);
        found != builtin_cpp_type_names().end()) {
        return found->second;
    }
    if (starts_with(type, "*const[") && ends_with(type, "]")) {
        return "const " + lower_cpp_type(type.substr(7, type.size() - 8)) + "*";
    }
    if (starts_with(type, "&const[") && ends_with(type, "]")) {
        return "const " + lower_cpp_type(type.substr(7, type.size() - 8)) + "&";
    }
    if (starts_with(type, "*")) {
        return lower_cpp_type(type.substr(1)) + "*";
    }
    if (starts_with(type, "&")) {
        return lower_cpp_type(type.substr(1)) + "&";
    }

    if (const auto array_type = lower_parsed_fixed_array_type(type)) {
        return *array_type;
    }

    if (const std::vector<std::string> dims = fixed_array_dimensions(type); !dims.empty()) {
        std::string out = lower_cpp_type(fixed_array_base(type));
        for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
            out = "std::array<" + out + ", " + *it + ">";
        }
        return out;
    }

    const size_t open = type.find('[');
    if (open != std::string::npos && ends_with(type, "]")) {
        const std::string name = type.substr(0, open);
        const std::string args = type.substr(open + 1, type.size() - open - 2);
        if (is_decimal_number(args) && name != "list" && name != "dict" && name != "set" &&
            name != "Option" && name != "Result" && name != "tuple" && name != "variant" &&
            name != "const" && name != "atomic" && name != "volatile") {
            return lower_cpp_type(name) + "[" + args + "]";
        }
        return lower_template_type(name, args);
    }
    return lower_cpp_type_name(type);
}

std::string lower_cpp_type(const std::string& raw_type, const CppEmitOptions& options) {
    const std::string type = trim_copy(raw_type);
    const std::string emitted = emitted_type_name(type, options);
    if (emitted != type) {
        return emitted;
    }
    return lower_cpp_type(type);
}

std::string lower_cpp_type(const std::string& raw_type,
                           const std::vector<std::string>& namespace_aliases) {
    return lower_cpp_type(strip_c_import_type_aliases(raw_type, namespace_aliases));
}

std::string lower_cpp_type(const std::string& raw_type,
                           const std::vector<std::string>& namespace_aliases,
                           const CppEmitOptions& options) {
    const std::string type = trim_copy(raw_type);
    const std::string emitted = emitted_type_name(type, options);
    if (emitted != type) {
        return emitted;
    }
    return lower_cpp_type(strip_c_import_type_aliases(type, namespace_aliases), options);
}

} // namespace dudu
