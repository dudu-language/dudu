#pragma once

#include "dudu/ast.hpp"

#include <string>
#include <utility>

namespace dudu {

inline NativeSymbolId native_identity(std::string canonical_path) {
    NativeSymbolId id;
    id.canonical_path = std::move(canonical_path);
    return id;
}

inline NativeSymbolId native_identity(std::string canonical_path, const std::string& source_file) {
    NativeSymbolId id = native_identity(std::move(canonical_path));
    if (!source_file.empty()) {
        id.usr = source_file + "::" + id.canonical_path;
    }
    return id;
}

} // namespace dudu
