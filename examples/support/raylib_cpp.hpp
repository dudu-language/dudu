#pragma once

#include "raylib.h"

namespace rl {
using ::Color;
using ::Vector2;

using ::BeginDrawing;
using ::ClearBackground;
using ::CloseWindow;
using ::DrawCircleV;
using ::DrawText;
using ::EndDrawing;
using ::GetFrameTime;
using ::InitWindow;
using ::IsKeyDown;
using ::SetTargetFPS;
using ::TextFormat;
using ::WindowShouldClose;

using ::KEY_DOWN;
using ::KEY_LEFT;
using ::KEY_RIGHT;
using ::KEY_UP;

inline Vector2 vec2(float x, float y) {
    return Vector2{x, y};
}

inline Color black() {
    return Color{0, 0, 0, 255};
}

inline Color gold() {
    return Color{255, 203, 0, 255};
}

inline Color skyblue() {
    return Color{102, 191, 255, 255};
}

inline Color white() {
    return Color{255, 255, 255, 255};
}
} // namespace rl
