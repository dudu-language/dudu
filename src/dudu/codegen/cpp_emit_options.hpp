#pragma once

#include "dudu/core/ast.hpp"

#include <cctype>
#include <map>
#include <string>
#include <string_view>

namespace dudu {

struct CppEmitOptions {
    bool emit_prelude = true;
    bool use_generated_names = false;
    bool test_source = false;
    bool expose_test_functions = false;
    std::map<std::string, std::string> generated_type_names;
    std::map<std::string, std::string> generated_value_names;
};

template <typename Decl>
const std::string& emitted_name(const Decl& decl, const CppEmitOptions& options) {
    if (options.use_generated_names && !decl.cpp_name.empty()) {
        return decl.cpp_name;
    }
    return decl.name;
}

inline std::string emitted_type_name(std::string name, const CppEmitOptions& options) {
    if (!options.use_generated_names) {
        return name;
    }
    if (const auto found = options.generated_type_names.find(name);
        found != options.generated_type_names.end()) {
        return found->second;
    }
    return name;
}

inline std::string emitted_value_name(std::string name, const CppEmitOptions& options) {
    if (!options.use_generated_names) {
        return name;
    }
    if (const auto found = options.generated_value_names.find(name);
        found != options.generated_value_names.end()) {
        return found->second;
    }
    return name;
}

inline std::string cpp_safe_identifier(std::string name) {
    for (char& c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
            c = '_';
        }
    }
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name.front())) != 0) {
        name.insert(name.begin(), '_');
    }
    return name;
}

inline std::string emitted_reserved_member_name(const std::string& owner, const std::string& member,
                                                const CppEmitOptions& options) {
    const std::string key = owner + "." + member;
    if (options.use_generated_names) {
        if (const auto found = options.generated_value_names.find(key);
            found != options.generated_value_names.end()) {
            return found->second;
        }
    }
    return cpp_safe_identifier(owner) + "_" + member;
}

inline bool cpp_reserved_identifier(std::string_view name) {
    return name == "alignas" || name == "alignof" || name == "and" || name == "and_eq" ||
           name == "asm" || name == "auto" || name == "bitand" || name == "bitor" ||
           name == "bool" || name == "break" || name == "case" || name == "catch" ||
           name == "char" || name == "char8_t" || name == "char16_t" || name == "char32_t" ||
           name == "class" || name == "compl" || name == "concept" || name == "const" ||
           name == "consteval" || name == "constexpr" || name == "constinit" ||
           name == "const_cast" || name == "continue" || name == "co_await" ||
           name == "co_return" || name == "co_yield" || name == "decltype" || name == "default" ||
           name == "delete" || name == "do" || name == "double" || name == "dynamic_cast" ||
           name == "else" || name == "enum" || name == "explicit" || name == "export" ||
           name == "extern" || name == "false" || name == "float" || name == "for" ||
           name == "friend" || name == "goto" || name == "if" || name == "inline" ||
           name == "int" || name == "long" || name == "mutable" || name == "namespace" ||
           name == "new" || name == "noexcept" || name == "not" || name == "not_eq" ||
           name == "nullptr" || name == "operator" || name == "or" || name == "or_eq" ||
           name == "private" || name == "protected" || name == "public" || name == "register" ||
           name == "reinterpret_cast" || name == "requires" || name == "return" ||
           name == "short" || name == "signed" || name == "sizeof" || name == "static" ||
           name == "static_assert" || name == "static_cast" || name == "struct" ||
           name == "switch" || name == "template" || name == "this" || name == "thread_local" ||
           name == "throw" || name == "true" || name == "try" || name == "typedef" ||
           name == "typeid" || name == "typename" || name == "union" || name == "unsigned" ||
           name == "using" || name == "virtual" || name == "void" || name == "volatile" ||
           name == "wchar_t" || name == "while" || name == "xor" || name == "xor_eq";
}

} // namespace dudu
