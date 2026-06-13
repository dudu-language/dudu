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

class BaseWidget {
  public:
    int base_scaled(int factor) const {
        return base_value * factor;
    }

    int base_value = 3;
};

class DerivedWidget : public BaseWidget {
  public:
    explicit DerivedWidget(int initial) {
        base_value = initial;
    }

    int derived_value = 7;
};

inline int use_base_widget(const BaseWidget* widget) {
    return widget->base_value;
}

inline int add(int a, int b) {
    return a + b;
}

inline bool ready() {
    return true;
}

inline int overloaded(int value) {
    return value;
}

inline float overloaded(float value) {
    return value;
}
} // namespace dudu_native

using DuduWidgetAlias = dudu_native::Widget;
using Widget = dudu_native::Widget;
