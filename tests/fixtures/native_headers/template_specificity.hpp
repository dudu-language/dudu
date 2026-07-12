#pragma once

namespace dudu_specificity {

using f32 = float;

template <typename T> struct Box {
    T value;
};

template <typename T> Box<T> make_box(T value) {
    return Box<T>{value};
}

template <typename T> Box<T> select(T left, T) {
    return left;
}

template <typename T> T select(const Box<T>& left, const Box<T>& right) {
    return left.value + right.value;
}

} // namespace dudu_specificity
