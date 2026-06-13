#pragma once

namespace dudu_native {
class Widget {
  public:
    Widget() = default;
    explicit Widget(int initial) : value(initial) {}

    int scaled(int factor) const {
        return value * factor;
    }

    int value = 0;
};

inline int add(int a, int b) {
    return a + b;
}

inline bool ready() {
    return true;
}
} // namespace dudu_native

using DuduWidgetAlias = dudu_native::Widget;
using Widget = dudu_native::Widget;
