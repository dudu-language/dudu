#pragma once

namespace dudu_tpl {

struct Box {
    int value{};

    explicit Box(int initial) : value(initial) {
    }

    template <typename T> T get() const {
        return static_cast<T>(value);
    }

    template <typename T> void set(T next) {
        value = static_cast<int>(next);
    }
};

} // namespace dudu_tpl
