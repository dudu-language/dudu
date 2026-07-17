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

struct __scope {
    template <typename T, typename U>
    struct __rebind {
        using type = Holder<U>;
    };
};

template <typename T, typename U>
using ScopedHolder = typename __scope::template __rebind<T, U>::type;

template <typename...>
using Void = void;

struct WithValue {
    using value_type = int;
};

struct WithoutValue {};

template <typename T, typename = void>
struct DetectedValue {
    using type = float;
};

template <typename T>
struct DetectedValue<
    T,
    // Comments, including quotes like "value", are not specialization arguments.
    Void<typename T::value_type>> {
    using type = typename T::value_type;
};

template <typename T>
typename __factory_result<T>::type make_holder();

template <typename... T>
struct Pack {};

template <size_t Index, typename T>
struct PackElement;

template <size_t Index, typename... T>
struct PackElement<Index, Pack<T...>> {
    using type = int;
};

template <size_t Index, typename... T>
typename PackElement<Index, Pack<T...>>::type make_pack_element();

template <size_t Index, typename... T>
struct SelectPack {};

template <typename T0, typename... Rest>
struct SelectPack<0, T0, Rest...> {
    using type = T0;
};

template <size_t Index, typename... T>
typename SelectPack<Index, T...>::type select_pack();

template <typename T, T Value>
struct Constant {
    static constexpr T value = Value;
};

using True = Constant<bool, true>;
using False = Constant<bool, false>;

template <typename T>
struct IsPointer : False {};

template <typename T>
struct IsPointer<T*> : True {};

template <bool>
struct Select {
    template <typename If, typename>
    using type = If;
};

template <>
struct Select<false> {
    template <typename, typename Else>
    using type = Else;
};

template <typename T>
using Selected = typename Select<IsPointer<T>::value>::template type<int, float>;

template <typename T>
Selected<T> select_result();

} // namespace internal_alias
