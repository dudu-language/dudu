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

class Outer {
  public:
    class Inner {
      public:
        explicit Inner(int initial) : value(initial) {}

        int doubled() const {
            return value * 2;
        }

        int value = 0;
    };
};

inline int use_base_widget(const BaseWidget* widget) {
    return widget->base_value;
}

inline int read_const_ptr(int const* value) {
    return *value;
}

inline int read_const_ref(Widget const& widget) {
    return widget.value;
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
