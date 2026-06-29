#pragma once

#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>

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

struct SourceLocation {
    SourceFileName file;
    int line = 1;
    int column = 1;
};

struct SourcePosition {
    int line = 1;
    int column = 1;

    SourcePosition() = default;
    SourcePosition(int line_in, int column_in) : line(line_in), column(column_in) {}
    SourcePosition(SourceLocation location) : line(location.line), column(location.column) {}

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

class CompileError : public std::runtime_error {
  public:
    CompileError(SourceLocation location, const std::string& message);
    CompileError(SourceLocation location, const std::string& message, std::string code,
                 std::string data_name = {});

    const SourceLocation& location() const {
        return location_;
    }

    const std::string& code() const {
        return code_;
    }

    const std::string& data_name() const {
        return data_name_;
    }

  private:
    SourceLocation location_;
    std::string code_;
    std::string data_name_;
};

std::string format_location(const SourceLocation& location);

} // namespace dudu
