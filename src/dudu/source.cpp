#include "dudu/source.hpp"

#include <mutex>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace dudu {
namespace {

const std::string& empty_file_name() {
    static const std::string empty;
    return empty;
}

const std::string* intern_file_name(std::string_view file) {
    if (file.empty()) {
        return {};
    }
    static std::mutex mutex;
    static std::unordered_set<std::string> files;
    std::lock_guard lock(mutex);
    const auto [it, inserted] = files.emplace(file);
    (void)inserted;
    return &*it;
}

} // namespace

SourceFileName::SourceFileName(const char* file)
    : file_(file == nullptr ? nullptr : intern_file_name(file)) {
}

SourceFileName::SourceFileName(std::string file) : file_(intern_file_name(file)) {
}

SourceFileName::SourceFileName(std::string_view file) : file_(intern_file_name(file)) {
}

const std::string& SourceFileName::str() const {
    return file_ == nullptr ? empty_file_name() : *file_;
}

bool SourceFileName::empty() const {
    return str().empty();
}

bool SourceFileName::ends_with(std::string_view suffix) const {
    return str().ends_with(suffix);
}

size_t SourceFileName::rfind(std::string_view needle, size_t position) const {
    return str().rfind(needle, position);
}

SourceFileName::operator std::string() const {
    return str();
}

SourceFileName::operator std::string_view() const {
    return str();
}

std::ostream& operator<<(std::ostream& out, const SourceFileName& file) {
    out << file.str();
    return out;
}

bool operator==(const SourceFileName& left, std::string_view right) {
    return std::string_view(left) == right;
}

bool operator==(std::string_view left, const SourceFileName& right) {
    return left == std::string_view(right);
}

bool operator!=(const SourceFileName& left, std::string_view right) {
    return !(left == right);
}

bool operator!=(std::string_view left, const SourceFileName& right) {
    return !(left == right);
}

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
