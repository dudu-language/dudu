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
    void insert(NativeCursorKind kind, std::string name, SourceLocation location, std::string usr,
                std::optional<TypeLayout> layout = std::nullopt,
                std::string semantic_path = {});
    std::optional<std::string> find(NativeCursorKind kind, std::string_view name,
                                    const SourceLocation& location) const;
    std::optional<TypeLayout> find_layout(NativeCursorKind kind, std::string_view name,
                                          const SourceLocation& location) const;
    std::optional<std::string> find_semantic(NativeCursorKind kind,
                                             std::string_view semantic_path) const;
    std::optional<TypeLayout> find_semantic_layout(NativeCursorKind kind,
                                                   std::string_view semantic_path) const;
    bool empty() const;
    std::string serialize() const;
    static NativeCursorIdentityIndex deserialize(std::string_view text);

  private:
    std::map<std::string, std::string> identities_;
    std::map<std::string, TypeLayout> layouts_;
};

NativeCursorIdentityIndex scan_native_cursor_identities(const std::filesystem::path& source,
                                                        const NativeHeaderOptions& options,
                                                        bool include_source_dir = true);

} // namespace dudu
