#pragma once

namespace dudu_op {

struct Vec2 {
    int x{};
    int y{};
};

inline Vec2 operator+(Vec2 left, Vec2 right) {
    return {left.x + right.x, left.y + right.y};
}

inline Vec2 operator+(Vec2 left, int value) {
    return {left.x + value, left.y + value};
}

inline bool operator==(Vec2 left, Vec2 right) {
    return left.x == right.x && left.y == right.y;
}

} // namespace dudu_op
