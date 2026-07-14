#pragma once

#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

class SourceFileName {
  public:
    SourceFileName() = default;
    explicit SourceFileName(const char* file);
    explicit SourceFileName(std::string file);
    explicit SourceFileName(std::string_view file);

    const std::string& str() const;
    bool empty() const;
    bool ends_with(std::string_view suffix) const;
    size_t rfind(std::string_view needle, size_t position = std::string::npos) const;

    operator std::string() const;
    operator std::string_view() const;

  private:
    uint32_t file_id_ = 0;
};

std::ostream& operator<<(std::ostream& out, const SourceFileName& file);
bool operator==(const SourceFileName& left, std::string_view right);
bool operator!=(const SourceFileName& left, std::string_view right);

class SourceTextAtom {
  public:
    SourceTextAtom() = default;
    explicit SourceTextAtom(const char* text);
    explicit SourceTextAtom(std::string text);
    explicit SourceTextAtom(std::string_view text);

    const std::string& str() const;
    bool empty() const;
    size_t size() const;
    const char* c_str() const;
    char operator[](size_t index) const;
    char front() const;
    char back() const;
    size_t find(std::string_view needle, size_t position = 0) const;
    size_t rfind(std::string_view needle, size_t position = std::string::npos) const;
    bool starts_with(std::string_view prefix) const;
    bool ends_with(std::string_view suffix) const;
    std::string substr(size_t position, size_t count = std::string::npos) const;
    std::string::const_iterator begin() const;
    std::string::const_iterator end() const;
    uint32_t id() const;

    SourceTextAtom& operator=(const char* text);
    SourceTextAtom& operator=(const std::string& text);
    SourceTextAtom& operator=(std::string_view text);
    SourceTextAtom& operator+=(std::string_view text);

    operator const std::string&() const;
    operator std::string_view() const;

  private:
    uint32_t text_id_ = 0;
};

std::ostream& operator<<(std::ostream& out, const SourceTextAtom& text);
bool operator==(const SourceTextAtom& left, std::string_view right);
bool operator==(std::string_view left, const SourceTextAtom& right);
bool operator==(const SourceTextAtom& left, const SourceTextAtom& right);
bool operator==(const SourceTextAtom& left, const char* right);
bool operator==(const char* left, const SourceTextAtom& right);
bool operator==(const SourceTextAtom& left, const std::string& right);
bool operator==(const std::string& left, const SourceTextAtom& right);
bool operator!=(const SourceTextAtom& left, std::string_view right);
bool operator!=(std::string_view left, const SourceTextAtom& right);
bool operator!=(const SourceTextAtom& left, const SourceTextAtom& right);
bool operator!=(const SourceTextAtom& left, const char* right);
bool operator!=(const char* left, const SourceTextAtom& right);
bool operator!=(const SourceTextAtom& left, const std::string& right);
bool operator!=(const std::string& left, const SourceTextAtom& right);
std::string operator+(const std::string& left, const SourceTextAtom& right);
std::string operator+(const SourceTextAtom& left, const std::string& right);
std::string operator+(const char* left, const SourceTextAtom& right);
std::string operator+(const SourceTextAtom& left, const char* right);

struct SourceLocation {
    SourceFileName file;
    int line = 1;
    int column = 1;
};

struct SourcePosition {
    int line = 1;
    int column = 1;

    SourcePosition() = default;
    SourcePosition(int line_in, int column_in) : line(line_in), column(column_in) {
    }
    SourcePosition(SourceLocation location) : line(location.line), column(location.column) {
    }

    SourcePosition& operator=(SourceLocation location) {
        line = location.line;
        column = location.column;
        return *this;
    }
};

struct SourceRange {
    SourceLocation start;
    SourcePosition end;
};

SourceLocation range_end_location(const SourceRange& range);

struct CompileNote {
    SourceLocation location;
    std::string message;
};

class CompileError : public std::runtime_error {
  public:
    CompileError(SourceLocation location, const std::string& message);
    CompileError(SourceLocation location, const std::string& message, std::string code,
                 std::string data_name = {});
    CompileError(SourceLocation location, const std::string& message, std::string code,
                 std::string data_name, std::vector<CompileNote> notes);

    const SourceLocation& location() const {
        return location_;
    }

    const std::string& code() const {
        return code_;
    }

    const std::string& data_name() const {
        return data_name_;
    }

    const std::vector<CompileNote>& notes() const {
        return notes_;
    }

  private:
    SourceLocation location_;
    std::string code_;
    std::string data_name_;
    std::vector<CompileNote> notes_;
};

std::string format_location(const SourceLocation& location);

} // namespace dudu
