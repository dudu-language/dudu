#include <cstdint>

int32_t add_three(int32_t value) {
    return value + 3;
}

int32_t xor_mask(int32_t value) {
    return value ^ 85;
}

using Callback = int32_t (*)(int32_t);

Callback choose_callback(bool use_mask) {
    if (use_mask) {
        return xor_mask;
    }
    return add_three;
}

int64_t callback_accum(int32_t n) {
    Callback callback = choose_callback((n & 1) != 0);
    int64_t total = 0;
    for (int32_t i = 0; i < n; ++i) {
        total += int64_t(callback(i & 1023));
    }
    return total;
}
