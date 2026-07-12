#pragma once

namespace internal_alias {

template <typename T>
struct Holder {
    T value{};
};

template <typename T>
struct __factory_result {
    using type = Holder<T>;
};

template <typename T, typename Fallback, typename = void>
struct __defaulted_result {
    using type = Fallback;
};

template <typename T>
typename __factory_result<T>::type make_holder();

} // namespace internal_alias
