#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace dudu_compat {

template <typename T, typename = void>
struct Traits {
    using value_type = T;
};

template <typename T>
struct Traits<T*, void> {
    using value_type = T;
};

template <>
struct Traits<int, void> {
    using value_type = long;
};

template <typename T>
T specialized(T value) {
    return value;
}

template <>
inline int specialized<int>(int value) {
    return value + 1;
}

template <std::integral T>
int constrained_kind(T) {
    return 1;
}

template <std::floating_point T>
int constrained_kind(T) {
    return 2;
}

template <typename T>
T defaulted(T value, T adjustment = T{1}) {
    return value + adjustment;
}

template <typename T, typename... Rest>
T packed_sum(T first, Rest... rest) {
    return (first + ... + rest);
}

struct Concrete {
    int value = 0;
};

template <typename T>
Concrete concrete_from(T value) {
    return Concrete{static_cast<int>(value)};
}

template <typename T>
int consume_concrete(Concrete concrete, T value) {
    return concrete.value + static_cast<int>(value);
}

struct ReferenceTarget {};

inline int reference_kind(ReferenceTarget&) {
    return 1;
}

inline int reference_kind(const ReferenceTarget&) {
    return 2;
}

inline int reference_kind(ReferenceTarget&&) {
    return 3;
}

template <typename T>
    requires(sizeof(T) > 0)
int ignores_deleted_candidate(T) {
    return 5;
}

template <typename T>
    requires(sizeof(T) == 0)
int ignores_deleted_candidate(T) = delete;

inline int deleted_only(int) = delete;

struct RefQualified {
    int value = 0;

    explicit RefQualified(int initial) : value(initial) {}

    int read() & {
        return value;
    }

    int read() const& {
        return value + 10;
    }

    int read() && {
        return value + 20;
    }
};

inline const RefQualified& const_ref(const RefQualified& value) {
    return value;
}

struct DeletedMethod {
    int available() const {
        return 1;
    }

    int unavailable() const = delete;
};

struct Multiplier {
    int factor = 1;

    int operator()(int value) const {
        return factor * value;
    }
};

struct GenericMultiplier {
    int factor = 1;

    template <typename Value>
    Value operator()(Value value) const {
        return static_cast<Value>(factor) * value;
    }
};

inline constexpr GenericMultiplier multiply_by_three{3};

struct __InternalCallable {
    int operator()(int value) const {
        return value + 2;
    }
};

inline constexpr __InternalCallable internal_add_two{};

struct Chain {
    int value = 0;

    explicit Chain(int initial) : value(initial) {}

    Chain child() const {
        return Chain(value + 1);
    }

    int read() const {
        return value;
    }
};

template <typename Callable, typename Value>
auto invoke_with(Callable&& callable, Value&& value)
    -> std::invoke_result_t<Callable, Value> {
    return std::invoke(std::forward<Callable>(callable), std::forward<Value>(value));
}

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

inline MoveOnly make_move_only(int value) {
    return MoveOnly(value);
}

template <typename T>
struct Base {
    T value;

    explicit Base(T initial) : value(initial) {}

    T get() const {
        return value;
    }
};

template <typename T>
struct Derived : Base<T> {
    using Base<T>::Base;

    T doubled() const {
        return this->value * T{2};
    }
};

template <typename Key, typename Value>
struct IndexedBase {
    Value values[4]{};

    Value& operator[](const Key& key) {
        return values[static_cast<unsigned>(key) % 4];
    }

    const Value& operator[](const Key& key) const {
        return values[static_cast<unsigned>(key) % 4];
    }
};

template <typename Key, typename Value>
struct IndexedDerived : IndexedBase<Key, Value> {
    using IndexedBase<Key, Value>::operator[];
};

struct DefaultTemplateMethod {
    template <typename Input, typename Result = Input>
    Result convert(Input value) const {
        return static_cast<Result>(value);
    }

    template <typename Input, typename... Extra>
    int count(Input, Extra...) const {
        return 1 + sizeof...(Extra);
    }
};

template <bool>
struct DefaultedKeyArgument {
    template <typename Candidate, typename>
    using type = Candidate;
};

template <typename Key, typename Mapped>
struct DefaultedAliasMethod {
    using key_type = Key;
    using mapped_type = Mapped;
    using key_argument = DefaultedKeyArgument<false>;

    template <typename Candidate>
    using key_arg = key_argument::type<Candidate, key_type>;

    template <typename Candidate = key_type, typename Value = mapped_type>
    int insert_or_assign(key_arg<Candidate>&&, Value&& value) {
        return static_cast<int>(value);
    }
};

namespace parameter_scope {

struct Value {};

template <typename Value>
using Identity = Value;

} // namespace parameter_scope

template <typename T, typename = void>
struct ForwardTraits;

template <typename T>
struct ForwardTraits<T, std::enable_if_t<std::is_integral_v<T>>> {
    using value_type = T;
};

struct ForwardDefault {
    using value_type = typename ForwardTraits<int>::value_type;

    value_type get() const {
        return 42;
    }
};

template <typename T>
struct DefaultBox {
    using value_type = T;
};

template <typename T, typename = void>
struct OmittedDefaultTraits;

template <typename T>
struct OmittedDefaultTraits<DefaultBox<T>> {
    using value_type = T;
};

struct OmittedDefaultUse {
    using value_type = typename OmittedDefaultTraits<DefaultBox<int>>::value_type;

    value_type get() const {
        return 7;
    }
};

template <typename T>
struct HasSizeType {
    static constexpr bool value = false;
};

struct SizeAwareAllocator {
    using size_type = unsigned long;
};

template <>
struct HasSizeType<SizeAwareAllocator> {
    static constexpr bool value = true;
};

template <typename Alloc, typename Difference, bool = HasSizeType<Alloc>::value>
struct DependentDefaultSize {
    using type = std::make_unsigned_t<Difference>;
};

template <typename Alloc, typename Difference>
struct DependentDefaultSize<Alloc, Difference, true> {
    using type = typename Alloc::size_type;
};

struct DependentDefaultUse {
    using size_type = typename DependentDefaultSize<SizeAwareAllocator, long>::type;

    size_type size() const {
        return 6;
    }
};

#define DUDU_COMPAT_DECLARE(NAME, OFFSET) \
    inline int NAME(int value) { return value + OFFSET; }

DUDU_COMPAT_DECLARE(macro_declared, 3)

} // namespace dudu_compat
