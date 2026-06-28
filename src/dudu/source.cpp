#include "dudu/source.hpp"

#include <sstream>

namespace dudu {

std::string format_location(const SourceLocation& location) {
    std::ostringstream out;
    if (!location.file.empty()) {
        out << location.file << ':';
    }
    out << location.line << ':' << location.column;
    return out.str();
}

CompileError::CompileError(SourceLocation location, const std::string& message)
    : std::runtime_error(format_location(location) + ": " + message),
      location_(std::move(location)) {
}

CompileError::CompileError(SourceLocation location, const std::string& message, std::string code,
                           std::string data_name)
    : std::runtime_error(format_location(location) + ": " + message),
      location_(std::move(location)), code_(std::move(code)), data_name_(std::move(data_name)) {
}

} // namespace dudu
