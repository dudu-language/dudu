#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/native/native_headers.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace dudu {

enum class NativeCursorKind {
    Type,
    Class,
    Function,
    Method,
    Constructor,
    Field,
    Value,
    Namespace,
};

class NativeCursorIdentityIndex {
  public:
    void insert(NativeCursorKind kind, std::string name, SourceLocation location,
                std::string usr);
    std::optional<std::string> find(NativeCursorKind kind, std::string_view name,
                                    const SourceLocation& location) const;
    bool empty() const;

  private:
    std::map<std::string, std::string> identities_;
};

NativeCursorIdentityIndex
scan_native_cursor_identities(const std::filesystem::path& source,
                              const NativeHeaderOptions& options,
                              bool include_source_dir = true);

} // namespace dudu
