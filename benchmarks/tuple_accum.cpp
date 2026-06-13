#include <cstdint>

struct Pair {
    int32_t q{};
    int32_t r{};
};

Pair divmod_pair(int32_t value, int32_t divisor) {
    return {value / divisor, value % divisor};
}

int64_t tuple_accum(int32_t n) {
    int64_t total = 0;
    for (int32_t i = 0; i < n; ++i) {
        const Pair pair = divmod_pair(i, 97);
        total += int64_t(pair.q + pair.r);
    }
    return total;
}
