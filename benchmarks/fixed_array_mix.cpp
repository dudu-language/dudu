#include <array>
#include <cstddef>
#include <cstdint>

int64_t fixed_array_mix(int32_t n) {
    std::array<int32_t, 8> values{};
    for (int32_t i = 0; i < 8; ++i) {
        values[static_cast<size_t>(i)] = i * 3 + 1;
    }

    int64_t total = 0;
    for (int32_t i = 0; i < n; ++i) {
        const int32_t index = i & 7;
        total += int64_t(values[static_cast<size_t>(index)]);
        values[static_cast<size_t>(index)] += 1;
    }
    return total;
}
