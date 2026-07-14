#pragma once

#include <concepts>
#include <memory>
#include <utility>

namespace dudu_modern {

template <typename T>
concept Addable = requires(T left, T right) {
    left + right;
};

template <Addable T>
T constrained_add(const T& left, const T& right) {
    return left + right;
}

template <typename T, typename... Rest>
T fold_sum(T first, Rest... rest) {
    return (first + ... + rest);
}

inline int default_sum(int first, int second = 2, int third = 3) {
    return first + second + third;
}

template <typename T>
struct Base {
    T value{};

    explicit Base(T initial) : value(initial) {}

    T get() const {
        return value;
    }

    template <typename U>
    T scaled(U factor) const {
        return static_cast<T>(value * factor);
    }
};

template <typename T>
struct Derived : Base<T> {
    using Base<T>::Base;
};

struct MoveOnly {
    std::unique_ptr<int> value;

    explicit MoveOnly(int initial) : value(std::make_unique<int>(initial)) {}
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;

    int get() const {
        return *value;
    }
};

inline void increment(int& value) {
    ++value;
}

inline const int* address_of(const int& value) {
    return &value;
}

} // namespace dudu_modern
