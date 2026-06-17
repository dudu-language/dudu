#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

struct CppEmitOptions {
    bool use_generated_names = false;
};

template <typename Decl>
const std::string& emitted_name(const Decl& decl, const CppEmitOptions& options) {
    if (options.use_generated_names && !decl.cpp_name.empty()) {
        return decl.cpp_name;
    }
    return decl.name;
}

} // namespace dudu
