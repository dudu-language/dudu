#include "dudu/language_server_lint_common.hpp"

#include "dudu/language_server_support.hpp"

#include <algorithm>
#include <vector>

namespace dudu {

bool lint_same_source_file(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    if (lhs.empty() || rhs.empty()) {
        return lhs == rhs;
    }
    std::error_code error;
    const std::filesystem::path lhs_canonical = std::filesystem::weakly_canonical(lhs, error);
    if (error) {
        return lhs.lexically_normal() == rhs.lexically_normal();
    }
    const std::filesystem::path rhs_canonical = std::filesystem::weakly_canonical(rhs, error);
    if (error) {
        return lhs.lexically_normal() == rhs.lexically_normal();
    }
    return lhs_canonical == rhs_canonical;
}

bool lint_same_source_file(const SourceFileName& lhs, const std::filesystem::path& rhs) {
    return lint_same_source_file(std::filesystem::path(lhs.str()), rhs);
}

SourceRange lint_delete_line_range(const SourceLocation& location, const Document& doc) {
    const std::vector<std::string> lines = document_lines(doc.text);
    const int zero_based_line = std::max(0, location.line - 1);
    if (zero_based_line + 1 < static_cast<int>(lines.size())) {
        return {.start = {.file = location.file, .line = zero_based_line + 1, .column = 1},
                .end = {.file = location.file, .line = zero_based_line + 2, .column = 1}};
    }
    const size_t line_index = std::min(static_cast<size_t>(std::max(0, zero_based_line)),
                                       lines.empty() ? 0 : lines.size() - 1);
    const int end_column = lines.empty() ? 1 : static_cast<int>(lines[line_index].size()) + 1;
    return {.start = {.file = location.file, .line = zero_based_line + 1, .column = 1},
            .end = {.file = location.file, .line = zero_based_line + 1, .column = end_column}};
}

} // namespace dudu
