#include "dudu/native_header_types.hpp"

#include "dudu/cpp_lower.hpp"

#include <string_view>

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
    if (type == "int8_t" || type == "Sint8" || type == "signed char")
        return "i8";
    if (type == "uint8_t" || type == "Uint8" || type == "unsigned char")
        return "u8";
    if (type == "int16_t" || type == "Sint16" || type == "short")
        return "i16";
    if (type == "uint16_t" || type == "Uint16" || type == "unsigned short")
        return "u16";
    if (type == "int" || type == "int32_t" || type == "Sint32")
        return "i32";
    if (type == "unsigned int" || type == "uint32_t" || type == "Uint32")
        return "u32";
    if (type == "long" || type == "long long" || type == "int64_t" || type == "Sint64")
        return "i64";
    if (type == "unsigned long" || type == "unsigned long long" || type == "uint64_t" ||
        type == "Uint64") {
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

} // namespace

std::string dudu_type(std::string type) {
    type = trim_copy(std::move(type));
    type = erase_all(std::move(type), "__restrict__");
    type = erase_all(std::move(type), "__restrict");
    type = erase_all(std::move(type), " restrict");
    type = trim_copy(std::move(type));
    if (type == "char *" || type == "const char *")
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
    bool is_const = false;
    while (starts_with(type, "const ") || ends_with(type, " const")) {
        is_const = true;
        if (starts_with(type, "const ")) {
            type = trim_copy(type.substr(6));
        } else {
            type = trim_copy(type.substr(0, type.size() - 6));
        }
    }
    for (const char* prefix : {"class ", "struct ", "union "}) {
        if (starts_with(type, prefix)) {
            type = trim_copy(type.substr(std::string_view(prefix).size()));
            break;
        }
    }
    std::string out = cpp_scope_to_dudu(scalar_dudu_type(type));
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
    for (std::string part : split_top_level_args(signature.substr(open + 1, close - open - 1))) {
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
