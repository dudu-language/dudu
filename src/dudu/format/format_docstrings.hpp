#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dudu {

struct FormatLine {
    std::string text;
    bool normalize_indent = true;
    bool trim_end = true;
    bool preserve_blank = false;
};

std::vector<FormatLine> prepare_format_lines(std::string_view source);

} // namespace dudu
