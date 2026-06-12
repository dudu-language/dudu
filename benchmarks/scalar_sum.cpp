#include <cstdint>

int64_t scalar_sum(int32_t n) {
    int64_t total = 0;
    for (int32_t i = 0; i < n; ++i) {
        total += int64_t(i) * 3;
    }
    return total;
}
