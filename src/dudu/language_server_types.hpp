#pragma once

#include "dudu/source.hpp"

#include <filesystem>
#include <string>

namespace dudu {

struct Document {
    std::string uri;
    std::filesystem::path path;
    std::string text;
};

struct Diagnostic {
    SourceLocation location;
    std::string message;
    std::string source;
    int severity = 1;
};

struct Symbol {
    std::string name;
    std::string detail;
    SourceLocation location;
    int kind = 13;
};

struct ReferenceLocation {
    std::string uri;
    std::string range;
};

struct TextEdit {
    std::string range;
    std::string new_text;
};

} // namespace dudu
