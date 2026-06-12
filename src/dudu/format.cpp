#include "dudu/format.hpp"

#include <sstream>
#include <string>

namespace dudu {
namespace {

void rtrim(std::string& line) {
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.pop_back();
    }
}

} // namespace

std::string format_source(std::string_view source) {
    std::istringstream in{std::string(source)};
    std::ostringstream out;
    std::string line;
    int blank_count = 0;
    while (std::getline(in, line)) {
        rtrim(line);
        if (line.empty()) {
            ++blank_count;
            continue;
        }
        const int blanks = std::min(blank_count, 2);
        for (int i = 0; i < blanks; ++i) {
            out << '\n';
        }
        blank_count = 0;
        out << line << '\n';
    }
    return out.str();
}

} // namespace dudu
