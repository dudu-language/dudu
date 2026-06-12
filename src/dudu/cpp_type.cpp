#include "dudu/cpp_lower.hpp"

#include <algorithm>
#include <cctype>
#include <map>
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

std::string lower_template_type(std::string_view name, const std::string& args) {
    if (name == "list") {
        return "std::vector<" + lower_cpp_type(args) + ">";
    }
    if (name == "dict") {
        return "std::unordered_map<" + replace_dots(args) + ">";
    }
    if (name == "set") {
        return "std::unordered_set<" + lower_cpp_type(args) + ">";
    }
    if (name == "Option") {
        return "std::optional<" + lower_cpp_type(args) + ">";
    }
    if (name == "Result") {
        std::ostringstream out;
        out << "dudu::Result<";
        const std::vector<std::string> parts = split_top_level_args(args);
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_cpp_type(parts[i]);
        }
        out << ">";
        return out.str();
    }
    if (name == "tuple") {
        std::ostringstream out;
        out << "std::tuple<";
        const std::vector<std::string> parts = split_top_level_args(args);
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_cpp_type(parts[i]);
        }
        out << ">";
        return out.str();
    }
    if (name == "const") {
        return "const " + lower_cpp_type(args);
    }
    if (name == "atomic") {
        return "std::atomic<" + lower_cpp_type(args) + ">";
    }
    if (name == "volatile") {
        return "volatile " + lower_cpp_type(args);
    }
    std::ostringstream out;
    out << replace_dots(std::string(name)) << "<";
    const std::vector<std::string> parts = split_top_level_args(args);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(parts[i]);
    }
    out << ">";
    return out.str();
}

std::string lower_function_type(const std::string& type) {
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

    std::ostringstream out;
    out << "std::function<" << result << '(';
    const std::vector<std::string> parts = split_top_level_args(args);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(parts[i]);
    }
    out << ")>";
    return out.str();
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

} // namespace dudu
