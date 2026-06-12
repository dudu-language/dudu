#include <cstdint>

int64_t pointer_sum(const int32_t* data, int32_t n) {
    int64_t total = 0;
    for (int32_t i = 0; i < n; ++i) {
        total += int64_t(data[i]);
    }
    return total;
}
