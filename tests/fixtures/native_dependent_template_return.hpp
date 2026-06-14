#pragma once

namespace dep {

template <typename T>
struct Box {
    T value{};

    Box() = default;
    explicit Box(T new_value) : value(new_value) {}

    T get() const {
        return value;
    }
};

template <typename T>
struct Holder {
    T value{};

    explicit Holder(T new_value) : value(new_value) {}

    Box<T> box() const {
        return Box<T>{value};
    }
};

} // namespace dep
