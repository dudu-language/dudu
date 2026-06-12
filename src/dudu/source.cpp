#include "dudu/source.hpp"

#include <sstream>

namespace dudu {

std::string format_location(const SourceLocation& location) {
    std::ostringstream out;
    if (!location.file.empty()) {
        out << location.file.string() << ':';
    }
    out << location.line << ':' << location.column;
    return out.str();
}

CompileError::CompileError(SourceLocation location, const std::string& message)
    : std::runtime_error(format_location(location) + ": " + message),
      location_(std::move(location)) {
}

} // namespace dudu
