#include "dudu/cpp_lower.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>

namespace dudu {
namespace {

std::string strip_c_import_type_aliases(std::string type,
                                        const std::vector<std::string>& namespace_aliases);

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

size_t find_matching_bracket(std::string_view text, const size_t open) {
    int depth = 1;
    size_t cursor = open + 1;
    while (cursor < text.size() && depth > 0) {
        if (text[cursor] == '[') {
            ++depth;
        } else if (text[cursor] == ']') {
            --depth;
        }
        ++cursor;
    }
    return depth == 0 ? cursor - 1 : std::string::npos;
}

std::optional<std::string> lower_canonical_array_type(const std::string& type) {
    if (!starts_with(type, "array[")) {
        return std::nullopt;
    }
    const size_t element_close = find_matching_bracket(type, 5);
    if (element_close == std::string::npos || element_close + 1 >= type.size() ||
        type[element_close + 1] != '[' || !ends_with(type, "]")) {
        return std::nullopt;
    }
    const size_t shape_open = element_close + 1;
    const size_t shape_close = find_matching_bracket(type, shape_open);
    if (shape_close != type.size() - 1) {
        return std::nullopt;
    }
    std::string out = lower_cpp_type(type.substr(6, element_close - 6));
    const std::vector<std::string> dims =
        split_top_level_args(type.substr(shape_open + 1, shape_close - shape_open - 1));
    if (dims.empty()) {
        return std::nullopt;
    }
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
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
    if (name == "const") {
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

std::string join_lowered_type_args(const std::vector<TypeRef>& args, size_t start = 0) {
    std::ostringstream out;
    for (size_t i = start; i < args.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << lower_cpp_type(args[i]);
    }
    return out.str();
}

std::string join_lowered_type_args(const std::vector<TypeRef>& args,
                                   const std::vector<std::string>& namespace_aliases,
                                   size_t start = 0) {
    std::ostringstream out;
    for (size_t i = start; i < args.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << lower_cpp_type(args[i], namespace_aliases);
    }
    return out.str();
}

std::string lower_template_type(const TypeRef& type) {
    const std::string& name = type.name;
    if (name == "list") {
        return "std::vector<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "span") {
        return "std::span<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "dict") {
        return "std::unordered_map<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "set") {
        return "std::unordered_set<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "Option") {
        return "std::optional<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "Result") {
        return "dudu::Result<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "tuple") {
        std::ostringstream out;
        out << "dudu::Tuple" << type.children.size() << "<" << join_lowered_type_args(type.children)
            << ">";
        return out.str();
    }
    std::ostringstream out;
    out << replace_dots(name) << "<" << join_lowered_type_args(type.children) << ">";
    return out.str();
}

std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases) {
    const std::string& name = type.name;
    if (name == "list") {
        return "std::vector<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "span") {
        return "std::span<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "dict") {
        return "std::unordered_map<" + join_lowered_type_args(type.children, namespace_aliases) +
               ">";
    }
    if (name == "set") {
        return "std::unordered_set<" + join_lowered_type_args(type.children, namespace_aliases) +
               ">";
    }
    if (name == "Option") {
        return "std::optional<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "Result") {
        return "dudu::Result<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "tuple") {
        std::ostringstream out;
        out << "dudu::Tuple" << type.children.size() << "<"
            << join_lowered_type_args(type.children, namespace_aliases) << ">";
        return out.str();
    }
    std::ostringstream out;
    out << replace_dots(strip_c_import_type_aliases(name, namespace_aliases)) << "<"
        << join_lowered_type_args(type.children, namespace_aliases) << ">";
    return out.str();
}

std::string lower_function_type(const TypeRef& type, bool pointer) {
    std::ostringstream signature;
    const std::string result = type.children.empty() ? "void" : lower_cpp_type(type.children[0]);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i]);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_function_type(const TypeRef& type, bool pointer,
                                const std::vector<std::string>& namespace_aliases) {
    std::ostringstream signature;
    const std::string result =
        type.children.empty() ? "void" : lower_cpp_type(type.children[0], namespace_aliases);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i], namespace_aliases);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_fixed_array_type(const TypeRef& type) {
    if (type.children.empty()) {
        return lower_cpp_type(type.text);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front());
    } else {
        out = lower_cpp_type(storage);
    }
    const std::vector<std::string> dims = split_top_level_args(type.value);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
}

std::string lower_fixed_array_type(const TypeRef& type,
                                   const std::vector<std::string>& namespace_aliases) {
    if (type.children.empty()) {
        return lower_cpp_type(type.text, namespace_aliases);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front(), namespace_aliases);
    } else {
        out = lower_cpp_type(storage, namespace_aliases);
    }
    const std::vector<std::string> dims = split_top_level_args(type.value);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
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

std::string strip_c_import_type_aliases(std::string type,
                                        const std::vector<std::string>& namespace_aliases) {
    for (const std::string& alias : namespace_aliases) {
        if (alias.empty() || alias.front() != '!') {
            continue;
        }
        const std::string name = alias.substr(1);
        if (name.empty() || name.find('.') != std::string::npos) {
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
    }
    return type;
}

} // namespace

std::string lower_cpp_type(const std::string& raw_type) {
    std::string type = trim_copy(raw_type);
    static const std::map<std::string, std::string> builtins = {
        {"bool", "bool"},       {"i8", "int8_t"},    {"i16", "int16_t"},
        {"i32", "int32_t"},     {"i64", "int64_t"},  {"u8", "uint8_t"},
        {"u16", "uint16_t"},    {"u32", "uint32_t"}, {"u64", "uint64_t"},
        {"isize", "intptr_t"},  {"usize", "size_t"}, {"f32", "float"},
        {"f64", "double"},      {"void", "void"},    {"str", "std::string"},
        {"cstr", "const char*"}};

    if (type.empty()) {
        return "void";
    }
    if (type == "None") {
        return "std::monostate";
    }
    if (starts_with(type, "fn(")) {
        return lower_function_type(type);
    }
    if (const auto found = builtins.find(type); found != builtins.end()) {
        return found->second;
    }
    if (starts_with(type, "*const[") && ends_with(type, "]")) {
        return lower_cpp_type(type.substr(7, type.size() - 8)) + " const*";
    }
    if (starts_with(type, "&const[") && ends_with(type, "]")) {
        return lower_cpp_type(type.substr(7, type.size() - 8)) + " const&";
    }
    if (starts_with(type, "*")) {
        return lower_cpp_type(type.substr(1)) + "*";
    }
    if (starts_with(type, "&")) {
        return lower_cpp_type(type.substr(1)) + "&";
    }

    if (const auto array_type = lower_canonical_array_type(type)) {
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
            name != "Option" && name != "Result" && name != "tuple" && name != "const" &&
            name != "atomic" && name != "volatile") {
            return lower_cpp_type(name) + "[" + args + "]";
        }
        return lower_template_type(name, args);
    }
    return replace_dots(type);
}

std::string lower_cpp_type(const std::string& raw_type,
                           const std::vector<std::string>& namespace_aliases) {
    return lower_cpp_type(strip_c_import_type_aliases(raw_type, namespace_aliases));
}

std::string lower_cpp_type(const TypeRef& type) {
    if (type.text.empty()) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return lower_cpp_type(type.text);
    case TypeKind::Value:
        return type.value.empty() ? type.text : type.value;
    case TypeKind::Template:
        return lower_template_type(type);
    case TypeKind::Pointer:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : lower_cpp_type(type.children[0]) + "*";
    case TypeKind::Reference:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : lower_cpp_type(type.children[0]) + "&";
    case TypeKind::Const:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : "const " + lower_cpp_type(type.children[0]);
    case TypeKind::Volatile:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : "volatile " + lower_cpp_type(type.children[0]);
    case TypeKind::Atomic:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : "std::atomic<" + lower_cpp_type(type.children[0]) + ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        return type.children.empty() ? lower_cpp_type(type.text) : lower_cpp_type(type.children[0]);
    case TypeKind::Static:
        return type.children.empty() ? lower_cpp_type(type.text) : lower_cpp_type(type.children[0]);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type);
    case TypeKind::Function:
        return lower_function_type(type, true);
    case TypeKind::Unknown:
        return lower_cpp_type(type.text);
    }
    return lower_cpp_type(type.text);
}

std::string lower_cpp_type(const TypeRef& type, const std::vector<std::string>& namespace_aliases) {
    if (namespace_aliases.empty()) {
        return lower_cpp_type(type);
    }
    if (type.text.empty()) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return lower_cpp_type(type.text, namespace_aliases);
    case TypeKind::Value:
        return type.value.empty() ? type.text : type.value;
    case TypeKind::Template:
        return lower_template_type(type, namespace_aliases);
    case TypeKind::Pointer:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases) + "*";
    case TypeKind::Reference:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases) + "&";
    case TypeKind::Const:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : "const " +
                                           lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Volatile:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : "volatile " +
                                           lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Atomic:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases)
                   : "std::atomic<" + lower_cpp_type(type.children[0], namespace_aliases) + ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Static:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type, namespace_aliases);
    case TypeKind::Function:
        return lower_function_type(type, true, namespace_aliases);
    case TypeKind::Unknown:
        return lower_cpp_type(type.text, namespace_aliases);
    }
    return lower_cpp_type(type.text, namespace_aliases);
}

} // namespace dudu
