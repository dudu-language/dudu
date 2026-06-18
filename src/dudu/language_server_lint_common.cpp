#include "dudu/language_server_lint_common.hpp"

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

} // namespace dudu
