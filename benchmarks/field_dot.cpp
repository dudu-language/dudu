#include <cstdint>

struct Vec2 {
    float x{};
    float y{};
};

float field_dot(Vec2 a, Vec2 b, int32_t n) {
    float total = 0.0F;
    for (int32_t i = 0; i < n; ++i) {
        total += a.x * b.x + a.y * b.y + float(i & 7);
    }
    return total;
}
