#pragma once

#include <string_view>

namespace dudu_native_string {
inline int size_of(std::string_view text) {
    return static_cast<int>(text.size());
}
} // namespace dudu_native_string
