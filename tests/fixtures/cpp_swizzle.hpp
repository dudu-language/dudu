#pragma once

namespace swiz {

struct Vec2 {
    int x;
    int y;
};

struct Vec4 {
    int x;
    int y;
    int z;
    int w;
};

inline Vec4 make_vec4(int x, int y, int z, int w) {
    return Vec4{x, y, z, w};
}

} // namespace swiz
