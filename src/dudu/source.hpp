#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace dudu {

struct SourceLocation {
    std::filesystem::path file;
    int line = 1;
    int column = 1;
};

struct SourceRange {
    SourceLocation start;
    SourceLocation end;
};

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
