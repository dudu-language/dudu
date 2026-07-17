#pragma once

#include <cstddef>

namespace depmeta {

template <typename Payload>
struct Wrapper {
    Payload value{};

    Payload get() const {
        return value;
    }
};

template <typename LeftPayload, typename RightPayload, std::size_t Extent>
struct Envelope {
    using selected_result = RightPayload;
    using wrapped_result = Wrapper<RightPayload>;

    LeftPayload left{};
    RightPayload right{};

    selected_result selected() const {
        return right;
    }

    wrapped_result wrapped() const {
        return Wrapper<RightPayload>{right};
    }

    static constexpr std::size_t extent() {
        return Extent;
    }
};

template <typename Payload, std::size_t Extent = 4>
struct DefaultedEnvelope {
    Payload value{};

    Payload get() const {
        return value;
    }
};

template <typename Allocator>
struct RebindTraits {
    template <typename Value>
    using rebind = Wrapper<Value>;
};

template <typename Value, typename Allocator>
struct NestedAliasOwner {
    using traits = RebindTraits<Allocator>;
    using result = typename traits::template rebind<Value>;
};

template <typename Selected, typename Left>
using AliasCarrier = Envelope<Left, Selected, 4>;

template <typename Payload, typename Holder = Wrapper<Payload>>
using DefaultedAlias = Holder;

template <typename Payload, typename Tag>
struct AliasByShape {
    using base_type = Wrapper<Payload>;
    using result = typename AliasByShape::base_type;

    result take(result value) const {
        return value;
    }
};

template <typename Payload>
struct AliasByShape<Wrapper<Payload>, void> {
    using base_type = Envelope<int, Payload, 1>;
    using result = typename AliasByShape::base_type;

    result take(result value) const {
        return value;
    }
};

template <bool Value>
struct BoolConstant {
    static constexpr bool value = Value;
};

template <bool Unique>
struct MapTraits {
    using unique_keys = BoolConstant<Unique>;
};

template <typename Value, typename Traits,
          bool Unique = Traits::unique_keys::value>
struct TraitDefaultBase {};

template <typename Value, typename Traits>
struct TraitDefaultBase<Value, Traits, true> {
    using mapped_type = Value;
};

template <typename Value, typename Traits>
struct TraitDefaultOwner : TraitDefaultBase<Value, Traits> {};

} // namespace depmeta
