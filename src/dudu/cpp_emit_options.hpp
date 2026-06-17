#pragma once

#include "dudu/ast.hpp"

#include <map>
#include <string>

namespace dudu {

struct CppEmitOptions {
    bool use_generated_names = false;
    std::map<std::string, std::string> generated_type_names;
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

} // namespace dudu
