#pragma once

namespace dudu_native {
class Widget {
  public:
    Widget() = default;
    explicit Widget(int initial) : value(initial) {
    }

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
        explicit Inner(int initial) : value(initial) {
        }

        int doubled() const {
            return value * 2;
        }

        int value = 0;
    };
};

template <typename... T> class PackValue {};

class PackHolder {
  public:
    template <typename... T> void accept(PackValue<T...> value) {
        (void)value;
    }
};

inline int use_base_widget(const BaseWidget* widget) {
    return widget->base_value;
}

inline int read_const_ptr(int const* value) {
    return *value;
}

inline int sum_ints(const int* values, int count) {
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += values[i];
    }
    return total;
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

inline int overloaded_pair(int first, float second) {
    return first + static_cast<int>(second);
}

inline int overloaded_pair(float first, int second) {
    return static_cast<int>(first) + second;
}

template <typename T, bool Enabled> class AssociatedResult {};

template <typename T> class AssociatedResult<T, true> {
  public:
    using type = T;
};

template <typename T> inline typename AssociatedResult<T, true>::type associated_identity(T value) {
    return value;
}

template <typename T> class PatternResult {};

template <typename T> class PatternResult<T*> {
  public:
    using type = T;
};

template <> class PatternResult<int> {
  public:
    using type = float;
};

template <bool Enabled, typename T> class AmbiguousResult {};

template <typename T> class AmbiguousResult<true, T> {
  public:
    using type = T;
};

template <typename T> class AmbiguousResult<false, T> {
  public:
    using type = T;
};

namespace associated_scope {
template <typename T> using NestedAssociatedResult = typename AssociatedResult<T, true>::type;

template <typename T> inline NestedAssociatedResult<T> nested_associated_identity(T value) {
    return value;
}
} // namespace associated_scope
} // namespace dudu_native

using DuduWidgetAlias = dudu_native::Widget;
using Widget = dudu_native::Widget;

inline int use_widget(const Widget* widget) {
    return widget->value;
}
