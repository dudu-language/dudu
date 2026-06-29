#include "dudu/source.hpp"

#include <cstdint>
#include <deque>
#include <stdexcept>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace dudu {
namespace {

const std::string& empty_file_name() {
    static const std::string empty;
    return empty;
}

struct FileNameInterner {
    std::mutex mutex;
    std::deque<std::string> files;
    std::unordered_map<std::string, uint32_t> ids;

    FileNameInterner() {
        files.emplace_back();
    }
};

FileNameInterner& file_name_interner() {
    static FileNameInterner interner;
    return interner;
}

uint32_t intern_file_name(std::string_view file) {
    if (file.empty()) {
        return 0;
    }
    FileNameInterner& interner = file_name_interner();
    std::lock_guard lock(interner.mutex);
    const std::string key(file);
    if (const auto found = interner.ids.find(key); found != interner.ids.end()) {
        return found->second;
    }
    if (interner.files.size() >= static_cast<size_t>(UINT32_MAX)) {
        throw std::runtime_error("too many source file names interned");
    }
    const uint32_t id = static_cast<uint32_t>(interner.files.size());
    interner.files.push_back(key);
    interner.ids.emplace(interner.files.back(), id);
    return id;
}

const std::string& file_name_from_id(uint32_t id) {
    if (id == 0) {
        return empty_file_name();
    }
    FileNameInterner& interner = file_name_interner();
    std::lock_guard lock(interner.mutex);
    if (id >= interner.files.size()) {
        return empty_file_name();
    }
    return interner.files[id];
}

} // namespace

SourceFileName::SourceFileName(const char* file)
    : file_id_(file == nullptr || file[0] == '\0' ? 0 : intern_file_name(file)) {
}

SourceFileName::SourceFileName(std::string file)
    : file_id_(file.empty() ? 0 : intern_file_name(file)) {
}

SourceFileName::SourceFileName(std::string_view file)
    : file_id_(file.empty() ? 0 : intern_file_name(file)) {
}

const std::string& SourceFileName::str() const {
    return file_name_from_id(file_id_);
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

bool operator!=(const SourceFileName& left, std::string_view right) {
    return !(left == right);
}

SourceLocation range_end_location(const SourceRange& range) {
    SourceLocation location = range.start;
    location.line = range.end.line;
    location.column = range.end.column;
    return location;
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
