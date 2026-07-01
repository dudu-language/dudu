#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace dudu {
struct Slice;
struct Ellipsis;
struct NewAxis;
} // namespace dudu

namespace ndad {

using i64 = std::int64_t;

struct IndexPlan {
    std::vector<i64> shape{};
    std::vector<i64> strides{};
    i64 offset{};
};

enum class IndexKind {
    Scalar,
    Slice,
    Ellipsis,
    NewAxis,
    Mask,
    Tensor,
};

struct IndexItem {
    IndexKind kind{};
    i64 scalar{};
    bool has_start{};
    bool has_end{};
    bool has_step{};
    i64 start{};
    i64 end{};
    i64 step{1};
    std::vector<i64> shape{};
};

template <class T>
concept SliceLike = requires(const T& value) {
    value.has_start;
    value.has_end;
    value.has_step;
    value.start;
    value.end;
    value.step;
};

template <class T>
concept EllipsisLike = std::is_same_v<std::decay_t<T>, dudu::Ellipsis>;

template <class T>
concept NewAxisLike = std::is_same_v<std::decay_t<T>, dudu::NewAxis>;

template <class T>
concept ShapedIndexLike = requires(const T& value) {
    value.shape;
};

template <class T>
concept BoolMaskIndexLike = requires(const T& value) {
    value.count_true();
};

template <class T>
std::vector<i64> as_i64_shape(const T& shape) {
    std::vector<i64> out;
    out.reserve(shape.size());
    for (const auto& dim : shape) {
        out.push_back(static_cast<i64>(dim));
    }
    return out;
}

template <class T>
IndexItem make_index_item(const T& value) {
    using Value = std::decay_t<T>;
    if constexpr (EllipsisLike<Value>) {
        return {.kind = IndexKind::Ellipsis};
    } else if constexpr (NewAxisLike<Value>) {
        return {.kind = IndexKind::NewAxis};
    } else if constexpr (SliceLike<Value>) {
        return {.kind = IndexKind::Slice,
                .has_start = value.has_start,
                .has_end = value.has_end,
                .has_step = value.has_step,
                .start = value.start,
                .end = value.end,
                .step = value.step};
    } else if constexpr (BoolMaskIndexLike<Value>) {
        return {.kind = IndexKind::Mask, .shape = {static_cast<i64>(value.count_true())}};
    } else if constexpr (ShapedIndexLike<Value>) {
        return {.kind = IndexKind::Tensor, .shape = as_i64_shape(value.shape)};
    } else {
        return {.kind = IndexKind::Scalar, .scalar = static_cast<i64>(value)};
    }
}

inline i64 normalize_scalar(i64 index, i64 extent) {
    if (index < 0) {
        index += extent;
    }
    return std::clamp(index, i64{0}, extent <= 0 ? i64{0} : extent - 1);
}

inline i64 slice_length(i64 start, i64 end, i64 step) {
    if (step == 0) {
        return 0;
    }
    if (step > 0) {
        if (end <= start) {
            return 0;
        }
        return ((end - start - 1) / step) + 1;
    }
    if (start <= end) {
        return 0;
    }
    return ((start - end - 1) / (-step)) + 1;
}

inline std::vector<IndexItem> expand_ellipsis(std::vector<IndexItem> items,
                                              const std::vector<i64>& shape) {
    bool saw_ellipsis = false;
    std::size_t consumed = 0;
    for (const IndexItem& item : items) {
        if (item.kind == IndexKind::Ellipsis) {
            saw_ellipsis = true;
            continue;
        }
        if (item.kind != IndexKind::NewAxis) {
            ++consumed;
        }
    }
    if (!saw_ellipsis) {
        return items;
    }
    const std::size_t fill = shape.size() > consumed ? shape.size() - consumed : 0;
    std::vector<IndexItem> out;
    for (const IndexItem& item : items) {
        if (item.kind != IndexKind::Ellipsis) {
            out.push_back(item);
            continue;
        }
        for (std::size_t i = 0; i < fill; ++i) {
            out.push_back({.kind = IndexKind::Slice});
        }
    }
    return out;
}

inline void append_full_axis(IndexPlan& out, const std::vector<i64>& shape,
                             const std::vector<i64>& strides, std::size_t& axis) {
    if (axis >= shape.size()) {
        return;
    }
    out.shape.push_back(shape[axis]);
    out.strides.push_back(strides[axis]);
    ++axis;
}

inline IndexPlan normalize_index(const std::vector<i64>& shape, const std::vector<i64>& strides,
                                 i64 offset, std::vector<IndexItem> items, bool cartesian) {
    items = expand_ellipsis(std::move(items), shape);
    IndexPlan out;
    out.offset = offset;
    std::size_t axis = 0;
    bool advanced_shape_added = false;
    for (const IndexItem& item : items) {
        switch (item.kind) {
        case IndexKind::Scalar: {
            if (axis >= shape.size()) {
                break;
            }
            out.offset += normalize_scalar(item.scalar, shape[axis]) * strides[axis];
            ++axis;
            break;
        }
        case IndexKind::Slice: {
            if (axis >= shape.size()) {
                break;
            }
            const i64 extent = shape[axis];
            const i64 step = item.has_step ? item.step : 1;
            i64 start = item.has_start ? item.start : (step < 0 ? extent - 1 : 0);
            i64 end = item.has_end ? item.end : (step < 0 ? -1 : extent);
            if (start < 0) {
                start += extent;
            }
            if (end < 0 && item.has_end) {
                end += extent;
            }
            out.offset += std::clamp(start, i64{0}, extent) * strides[axis];
            out.shape.push_back(slice_length(start, end, step));
            out.strides.push_back(strides[axis] * step);
            ++axis;
            break;
        }
        case IndexKind::NewAxis:
            out.shape.push_back(1);
            out.strides.push_back(0);
            break;
        case IndexKind::Mask:
            if (axis < shape.size()) {
                out.shape.insert(out.shape.end(), item.shape.begin(), item.shape.end());
                out.strides.push_back(1);
                ++axis;
            }
            break;
        case IndexKind::Tensor:
            if (axis < shape.size()) {
                if (cartesian || !advanced_shape_added) {
                    out.shape.insert(out.shape.end(), item.shape.begin(), item.shape.end());
                    out.strides.insert(out.strides.end(), item.shape.size(), 1);
                    advanced_shape_added = true;
                }
                ++axis;
            }
            break;
        case IndexKind::Ellipsis:
            break;
        }
    }
    while (axis < shape.size()) {
        append_full_axis(out, shape, strides, axis);
    }
    return out;
}

template <class Shape, class Strides, class... Idx>
IndexPlan index_plan(const Shape& shape, const Strides& strides, i64 offset, const Idx&... idx) {
    std::vector<IndexItem> items{make_index_item(idx)...};
    return normalize_index(as_i64_shape(shape), as_i64_shape(strides), offset, std::move(items),
                           false);
}

template <class Shape, class Strides, class... Idx>
IndexPlan cartesian_index_plan(const Shape& shape, const Strides& strides, i64 offset,
                               const Idx&... idx) {
    std::vector<IndexItem> items{make_index_item(idx)...};
    return normalize_index(as_i64_shape(shape), as_i64_shape(strides), offset, std::move(items),
                           true);
}

} // namespace ndad
