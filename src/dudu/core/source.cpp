#include "dudu/core/source.hpp"

#include <array>
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

struct SourceTextInterner {
    std::mutex mutex;
    std::deque<std::string> texts;
    std::unordered_map<std::string, uint32_t> ids;

    SourceTextInterner() {
        texts.emplace_back();
    }
};

FileNameInterner& file_name_interner() {
    static FileNameInterner interner;
    return interner;
}

SourceTextInterner& source_text_interner() {
    static SourceTextInterner interner;
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

uint32_t intern_source_text(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    SourceTextInterner& interner = source_text_interner();
    std::lock_guard lock(interner.mutex);
    const std::string key(text);
    if (const auto found = interner.ids.find(key); found != interner.ids.end()) {
        return found->second;
    }
    if (interner.texts.size() >= static_cast<size_t>(UINT32_MAX)) {
        throw std::runtime_error("too many source text atoms interned");
    }
    const uint32_t id = static_cast<uint32_t>(interner.texts.size());
    interner.texts.push_back(key);
    interner.ids.emplace(interner.texts.back(), id);
    return id;
}

const std::string& source_text_from_id(uint32_t id) {
    if (id == 0) {
        return empty_file_name();
    }
    constexpr size_t cache_size = 64;
    const size_t cache_slot = id % cache_size;
    static thread_local std::array<uint32_t, cache_size> cached_ids{};
    static thread_local std::array<const std::string*, cache_size> cached_texts{};
    if (id == cached_ids[cache_slot] && cached_texts[cache_slot] != nullptr) {
        return *cached_texts[cache_slot];
    }
    SourceTextInterner& interner = source_text_interner();
    std::lock_guard lock(interner.mutex);
    if (id >= interner.texts.size()) {
        return empty_file_name();
    }
    cached_ids[cache_slot] = id;
    cached_texts[cache_slot] = &interner.texts[id];
    return *cached_texts[cache_slot];
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

SourceTextAtom::SourceTextAtom(const char* text)
    : text_id_(text == nullptr || text[0] == '\0' ? 0 : intern_source_text(text)) {
}

SourceTextAtom::SourceTextAtom(std::string text)
    : text_id_(text.empty() ? 0 : intern_source_text(text)) {
}

SourceTextAtom::SourceTextAtom(std::string_view text)
    : text_id_(text.empty() ? 0 : intern_source_text(text)) {
}

const std::string& SourceTextAtom::str() const {
    return source_text_from_id(text_id_);
}

bool SourceTextAtom::empty() const {
    return str().empty();
}

size_t SourceTextAtom::size() const {
    return str().size();
}

const char* SourceTextAtom::c_str() const {
    return str().c_str();
}

char SourceTextAtom::operator[](size_t index) const {
    return str()[index];
}

char SourceTextAtom::front() const {
    return str().front();
}

char SourceTextAtom::back() const {
    return str().back();
}

size_t SourceTextAtom::find(std::string_view needle, size_t position) const {
    return str().find(needle, position);
}

size_t SourceTextAtom::rfind(std::string_view needle, size_t position) const {
    return str().rfind(needle, position);
}

bool SourceTextAtom::starts_with(std::string_view prefix) const {
    return str().starts_with(prefix);
}

bool SourceTextAtom::ends_with(std::string_view suffix) const {
    return str().ends_with(suffix);
}

std::string SourceTextAtom::substr(size_t position, size_t count) const {
    return str().substr(position, count);
}

std::string::const_iterator SourceTextAtom::begin() const {
    return str().begin();
}

std::string::const_iterator SourceTextAtom::end() const {
    return str().end();
}

uint32_t SourceTextAtom::id() const {
    return text_id_;
}

SourceTextAtom& SourceTextAtom::operator=(const char* text) {
    text_id_ = text == nullptr || text[0] == '\0' ? 0 : intern_source_text(text);
    return *this;
}

SourceTextAtom& SourceTextAtom::operator=(const std::string& text) {
    text_id_ = text.empty() ? 0 : intern_source_text(text);
    return *this;
}

SourceTextAtom& SourceTextAtom::operator=(std::string_view text) {
    text_id_ = text.empty() ? 0 : intern_source_text(text);
    return *this;
}

SourceTextAtom& SourceTextAtom::operator+=(std::string_view text) {
    if (text.empty()) {
        return *this;
    }
    std::string combined = str();
    combined += text;
    text_id_ = intern_source_text(combined);
    return *this;
}

SourceTextAtom::operator const std::string&() const {
    return str();
}

SourceTextAtom::operator std::string_view() const {
    return str();
}

std::ostream& operator<<(std::ostream& out, const SourceTextAtom& text) {
    out << text.str();
    return out;
}

bool operator==(const SourceTextAtom& left, std::string_view right) {
    return std::string_view(left) == right;
}

bool operator==(std::string_view left, const SourceTextAtom& right) {
    return left == std::string_view(right);
}

bool operator==(const SourceTextAtom& left, const SourceTextAtom& right) {
    return left.id() == right.id();
}

bool operator==(const SourceTextAtom& left, const char* right) {
    return left.str() == (right == nullptr ? std::string_view{} : std::string_view{right});
}

bool operator==(const char* left, const SourceTextAtom& right) {
    return right == left;
}

bool operator==(const SourceTextAtom& left, const std::string& right) {
    return left.str() == right;
}

bool operator==(const std::string& left, const SourceTextAtom& right) {
    return right == left;
}

bool operator!=(const SourceTextAtom& left, std::string_view right) {
    return !(left == right);
}

bool operator!=(std::string_view left, const SourceTextAtom& right) {
    return !(left == right);
}

bool operator!=(const SourceTextAtom& left, const SourceTextAtom& right) {
    return !(left == right);
}

bool operator!=(const SourceTextAtom& left, const char* right) {
    return !(left == right);
}

bool operator!=(const char* left, const SourceTextAtom& right) {
    return !(left == right);
}

bool operator!=(const SourceTextAtom& left, const std::string& right) {
    return !(left == right);
}

bool operator!=(const std::string& left, const SourceTextAtom& right) {
    return !(left == right);
}

std::string operator+(const std::string& left, const SourceTextAtom& right) {
    std::string out = left;
    out += right.str();
    return out;
}

std::string operator+(const SourceTextAtom& left, const std::string& right) {
    std::string out = left.str();
    out += right;
    return out;
}

std::string operator+(const char* left, const SourceTextAtom& right) {
    std::string out = left == nullptr ? std::string{} : std::string{left};
    out += right.str();
    return out;
}

std::string operator+(const SourceTextAtom& left, const char* right) {
    std::string out = left.str();
    if (right != nullptr) {
        out += right;
    }
    return out;
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
