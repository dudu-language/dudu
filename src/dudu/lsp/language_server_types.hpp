#pragma once

#include "dudu/core/source.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

namespace lsp_symbol_kind {
inline constexpr int File = 1;
inline constexpr int Module = 2;
inline constexpr int Namespace = 3;
inline constexpr int Class = 5;
inline constexpr int Method = 6;
inline constexpr int Field = 8;
inline constexpr int Constructor = 9;
inline constexpr int Enum = 10;
inline constexpr int Function = 12;
inline constexpr int Variable = 13;
inline constexpr int Constant = 14;
inline constexpr int EnumMember = 22;
inline constexpr int Struct = 23;
} // namespace lsp_symbol_kind

struct Document {
    std::string uri;
    std::filesystem::path path;
    std::string text;
    int version = 0;
};

struct DiagnosticRelatedInformation {
    SourceLocation location;
    std::string message;
};

struct Diagnostic {
    SourceLocation location;
    std::string message;
    std::string source;
    int severity = 1;
    std::string code;
    std::string data_name;
    std::optional<SourceRange> fix_range;
    std::vector<DiagnosticRelatedInformation> related_information;
};

struct Symbol {
    std::string name;
    std::string detail;
    SourceLocation location;
    int kind = lsp_symbol_kind::Variable;
    std::optional<std::string> native_identity_key;
    std::string doc_comment{};
};

struct ReferenceLocation {
    std::string uri;
    std::string range;
    SourceRange source_range;
};

struct TextEdit {
    std::string range;
    std::string new_text;
};

} // namespace dudu
