#pragma once

namespace dudu_template {

template <typename T> T identity(T value) {
    return value;
}

template <typename T> T add_one(T value) {
    return value + static_cast<T>(1);
}

} // namespace dudu_template
