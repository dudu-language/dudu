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

template <typename Selected, typename Left>
using AliasCarrier = Envelope<Left, Selected, 4>;

template <typename Payload, typename Holder = Wrapper<Payload>>
using DefaultedAlias = Holder;

} // namespace depmeta
