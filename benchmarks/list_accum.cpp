#include <cstdint>
#include <vector>

int64_t list_accum(int32_t n) {
    std::vector<int32_t> values{};
    for (int32_t i = 0; i < n; ++i) {
        values.push_back(i & 1023);
    }

    int64_t total = 0;
    for (int32_t value : values) {
        total += int64_t(value);
    }
    return total;
}
