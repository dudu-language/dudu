#pragma once

namespace dudu_ctor {

struct Widget {
    int value{};

    Widget() : value(1) {
    }
    explicit Widget(int x) : value(x) {
    }
    Widget(int x, int y) : value(x + y) {
    }
};

} // namespace dudu_ctor
