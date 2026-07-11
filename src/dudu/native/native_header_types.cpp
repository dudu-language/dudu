#include "dudu/native/native_header_types.hpp"

#include "dudu/codegen/cpp_lower.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

std::string scalar_dudu_type(const std::string& type) {
    if (type == "bool")
        return "bool";
    if (type == "void")
        return "void";
    if (type == "float")
        return "f32";
    if (type == "double")
        return "f64";
    if (type == "size_t")
        return "usize";
    if (type == "char")
        return "char";
    if (type == "int8_t" || type == "signed char")
        return "i8";
    if (type == "uint8_t" || type == "unsigned char")
        return "u8";
    if (type == "int16_t" || type == "short")
        return "i16";
    if (type == "uint16_t" || type == "unsigned short")
        return "u16";
    if (type == "int" || type == "int32_t")
        return "i32";
    if (type == "unsigned int" || type == "uint32_t")
        return "u32";
    if (type == "long" || type == "long long" || type == "int64_t")
        return "i64";
    if (type == "unsigned long" || type == "unsigned long long" || type == "uint64_t") {
        return "u64";
    }
    return type.empty() ? "auto" : type;
}

std::string erase_all(std::string text, std::string_view token) {
    size_t pos = text.find(token);
    while (pos != std::string::npos) {
        text.erase(pos, token.size());
        pos = text.find(token, pos);
    }
    return text;
}

std::string cpp_scope_to_dudu(std::string type) {
    size_t pos = type.find("::");
    while (pos != std::string::npos) {
        type.replace(pos, 2, ".");
        pos = type.find("::", pos + 1);
    }
    return type;
}

size_t matching_paren(std::string_view text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '(') {
            ++depth;
        } else if (text[i] == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

size_t matching_angle(std::string_view text, size_t open) {
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
    return std::string::npos;
}

std::string collapse_template_associated_type(std::string type) {
    constexpr std::string_view typename_prefix = "typename ";
    if (starts_with(type, typename_prefix)) {
        type = trim_copy(type.substr(typename_prefix.size()));
    }
    const size_t open = type.find('<');
    if (open == std::string::npos) {
        return type;
    }
    const size_t close = matching_angle(type, open);
    if (close == std::string::npos) {
        return type;
    }
    std::string suffix = trim_copy(type.substr(close + 1));
    if (suffix.starts_with("::")) {
        suffix = trim_copy(suffix.substr(2));
    } else if (suffix.starts_with(".")) {
        suffix = trim_copy(suffix.substr(1));
    } else {
        return type;
    }
    if (suffix.empty() || suffix.find_first_not_of(
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
                              std::string::npos) {
        return type;
    }
    return dudu_type(trim_copy(type.substr(0, close + 1))) + "." + suffix;
}

std::vector<std::string> split_cpp_top_level_args(std::string_view args) {
    std::vector<std::string> out;
    int paren_depth = 0;
    int angle_depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t start = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        const char c = args[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '<') {
            ++angle_depth;
        } else if (c == '>' && angle_depth > 0) {
            --angle_depth;
        } else if (c == '(' || c == '[' || c == '{') {
            ++paren_depth;
        } else if ((c == ')' || c == ']' || c == '}') && paren_depth > 0) {
            --paren_depth;
        } else if (c == ',' && paren_depth == 0 && angle_depth == 0) {
            out.push_back(trim_copy(std::string(args.substr(start, i - start))));
            start = i + 1;
        }
    }
    const std::string last = trim_copy(std::string(args.substr(start)));
    if (!last.empty()) {
        out.push_back(last);
    }
    return out;
}

std::string lower_template_type(std::string type) {
    const size_t open = type.find('<');
    if (open == std::string::npos) {
        return cpp_scope_to_dudu(scalar_dudu_type(type));
    }
    const size_t close = matching_angle(type, open);
    if (close == std::string::npos || trim_copy(type.substr(close + 1)).empty() == false) {
        return cpp_scope_to_dudu(scalar_dudu_type(type));
    }
    std::string out = cpp_scope_to_dudu(scalar_dudu_type(trim_copy(type.substr(0, open))));
    out.push_back('[');
    bool first = true;
    for (std::string arg : split_cpp_top_level_args(type.substr(open + 1, close - open - 1))) {
        if (!first) {
            out += ", ";
        }
        first = false;
        out += dudu_type(std::move(arg));
    }
    out.push_back(']');
    return out;
}

std::optional<std::pair<std::string, std::vector<std::string>>>
split_suffix_array_type(std::string type) {
    std::vector<std::string> dims;
    type = trim_copy(std::move(type));
    while (type.ends_with("]")) {
        const size_t open = type.rfind('[');
        if (open == std::string::npos) {
            return std::nullopt;
        }
        const std::string dim = trim_copy(type.substr(open + 1, type.size() - open - 2));
        if (dim.empty()) {
            return std::nullopt;
        }
        dims.insert(dims.begin(), dim);
        type = trim_copy(type.substr(0, open));
    }
    if (dims.empty() || type.empty()) {
        return std::nullopt;
    }
    return std::pair{type, dims};
}

std::string lower_suffix_array_type(std::string base, const std::vector<std::string>& dims) {
    std::string out = "array[" + dudu_type(std::move(base)) + "][";
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += dims[i];
    }
    out += "]";
    return out;
}

} // namespace

std::string dudu_type(std::string type) {
    type = trim_copy(std::move(type));
    type = erase_all(std::move(type), "__restrict__");
    type = erase_all(std::move(type), "__restrict");
    type = erase_all(std::move(type), " restrict");
    type = trim_copy(std::move(type));
    if (type.ends_with("...")) {
        return dudu_type(trim_copy(type.substr(0, type.size() - 3))) + "...";
    }
    if (type == "const char *")
        return "cstr";
    if (type.find("(*)") != std::string::npos || type.find("(*") != std::string::npos) {
        return "*void";
    }
    int pointer_depth = 0;
    bool reference = false;
    while (!type.empty()) {
        if (type.back() == '*') {
            ++pointer_depth;
            type = trim_copy(type.substr(0, type.size() - 1));
        } else if (type.back() == '&') {
            reference = true;
            type = trim_copy(type.substr(0, type.size() - 1));
        } else {
            break;
        }
    }
    type = collapse_template_associated_type(std::move(type));
    bool is_const = false;
    while (starts_with(type, "const ") || ends_with(type, " const")) {
        is_const = true;
        if (starts_with(type, "const ")) {
            type = trim_copy(type.substr(6));
        } else {
            type = trim_copy(type.substr(0, type.size() - 6));
        }
    }
    for (const char* prefix : {"class ", "struct ", "union ", "enum "}) {
        if (starts_with(type, prefix)) {
            type = trim_copy(type.substr(std::string_view(prefix).size()));
            break;
        }
    }
    if (const auto array_type = split_suffix_array_type(type)) {
        std::string out = lower_suffix_array_type(array_type->first, array_type->second);
        if (is_const) {
            out = "const[" + out + "]";
        }
        return reference ? "&" + out : out;
    }
    std::string out = lower_template_type(type);
    if (is_const)
        out = "const[" + out + "]";
    for (int i = 0; i < pointer_depth; ++i)
        out = "*" + out;
    return reference ? "&" + out : out;
}

std::vector<std::string> signature_params(const std::string& signature) {
    const size_t open = signature.find('(');
    const size_t close =
        open == std::string::npos ? std::string::npos : matching_paren(signature, open);
    if (open == std::string::npos || close == std::string::npos || close <= open + 1)
        return {};
    std::vector<std::string> out;
    for (std::string part :
         split_cpp_top_level_args(signature.substr(open + 1, close - open - 1))) {
        if (part != "...")
            out.push_back(dudu_type(std::move(part)));
    }
    return out;
}

std::string signature_return_type(const std::string& signature) {
    const size_t open = signature.find('(');
    return dudu_type(open == std::string::npos ? signature : signature.substr(0, open));
}

} // namespace dudu
