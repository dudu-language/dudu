#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
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
    std::vector<i64> explicit_offsets{};
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
    std::vector<i64> values{};
};

enum class ContributionKind {
    Scalar,
    Slice,
    Advanced,
};

struct OffsetContribution {
    std::size_t axis{};
    ContributionKind kind{};
    i64 scalar{};
    std::vector<i64> values{};
    std::size_t result_dim{};
    std::size_t result_rank{};
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
concept ShapedIndexLike = requires(const T& value) { value.shape; };

template <class T>
concept MaskIndexLike = requires(T& value) {
    value.data;
    value.count_true();
};

template <class T> std::vector<i64> as_i64_shape(const T& shape) {
    std::vector<i64> out;
    out.reserve(shape.size());
    for (const auto& dim : shape) {
        out.push_back(static_cast<i64>(dim));
    }
    return out;
}

inline i64 element_count(const std::vector<i64>& shape) {
    i64 total = 1;
    for (const i64 dim : shape) {
        total *= dim;
    }
    return shape.empty() ? 1 : total;
}

inline std::vector<i64> contiguous_strides(const std::vector<i64>& shape) {
    std::vector<i64> strides(shape.size(), 1);
    if (shape.empty()) {
        return strides;
    }
    for (std::size_t index = shape.size() - 1; index > 0; --index) {
        strides[index - 1] = strides[index] * shape[index];
    }
    return strides;
}

template <class... Dims> std::vector<i64> shape_from_dims(const Dims&... dims) {
    return {static_cast<i64>(dims)...};
}

inline i64 flat_offset(const std::vector<i64>& shape, const std::vector<i64>& strides, i64 offset,
                       i64 flat) {
    i64 source = offset;
    for (std::size_t dim = shape.size(); dim > 0; --dim) {
        const std::size_t axis = dim - 1;
        const i64 extent = shape[axis];
        const i64 coord = extent == 0 ? 0 : flat % extent;
        flat = extent == 0 ? 0 : flat / extent;
        source += coord * strides[axis];
    }
    return source;
}

inline std::vector<i64> unravel_index(const std::vector<i64>& shape, i64 flat);

inline bool broadcast_compatible(const std::vector<i64>& left, const std::vector<i64>& right) {
    const std::size_t rank = std::max(left.size(), right.size());
    for (std::size_t i = 0; i < rank; ++i) {
        const i64 left_dim = i < left.size() ? left[left.size() - 1 - i] : 1;
        const i64 right_dim = i < right.size() ? right[right.size() - 1 - i] : 1;
        if (left_dim != right_dim && left_dim != 1 && right_dim != 1) {
            return false;
        }
    }
    return true;
}

inline std::vector<i64> broadcast_shape(const std::vector<i64>& left,
                                        const std::vector<i64>& right) {
    if (!broadcast_compatible(left, right)) {
        throw std::runtime_error("incompatible tensor broadcast shapes");
    }
    const std::size_t rank = std::max(left.size(), right.size());
    std::vector<i64> out(rank, 1);
    for (std::size_t i = 0; i < rank; ++i) {
        const std::size_t result_axis = rank - 1 - i;
        const i64 left_dim = i < left.size() ? left[left.size() - 1 - i] : 1;
        const i64 right_dim = i < right.size() ? right[right.size() - 1 - i] : 1;
        out[result_axis] = std::max(left_dim, right_dim);
    }
    return out;
}

inline std::vector<i64> left_broadcast_shape(const std::vector<i64>& left,
                                             const std::vector<i64>& right) {
    const std::vector<i64> out = broadcast_shape(left, right);
    if (out != left) {
        throw std::runtime_error("right-hand tensor does not broadcast into left-hand shape");
    }
    return out;
}

inline i64 broadcast_flat_offset(const std::vector<i64>& source_shape,
                                 const std::vector<i64>& source_strides, i64 offset,
                                 const std::vector<i64>& result_shape, i64 flat) {
    std::vector<i64> result_coords = unravel_index(result_shape, flat);
    i64 source = offset;
    const std::size_t result_rank = result_shape.size();
    const std::size_t source_rank = source_shape.size();
    for (std::size_t source_axis = 0; source_axis < source_rank; ++source_axis) {
        const std::size_t result_axis = result_rank - source_rank + source_axis;
        const i64 coord = source_shape[source_axis] == 1 ? 0 : result_coords[result_axis];
        source += coord * source_strides[source_axis];
    }
    return source;
}

template <class Data, class Shape, class Strides, class Op>
Data binary_data(const Data& left_data, const Shape& left_shape_ref,
                 const Strides& left_strides_ref, i64 left_offset, const Data& right_data,
                 const Shape& right_shape_ref, const Strides& right_strides_ref, i64 right_offset,
                 Op op) {
    const std::vector<i64> left_shape = as_i64_shape(left_shape_ref);
    const std::vector<i64> right_shape = as_i64_shape(right_shape_ref);
    const std::vector<i64> result_shape = broadcast_shape(left_shape, right_shape);
    const std::vector<i64> left_strides = as_i64_shape(left_strides_ref);
    const std::vector<i64> right_strides = as_i64_shape(right_strides_ref);
    Data out;
    const i64 count = element_count(result_shape);
    out.reserve(static_cast<std::size_t>(count));
    for (i64 flat = 0; flat < count; ++flat) {
        const i64 left =
            broadcast_flat_offset(left_shape, left_strides, left_offset, result_shape, flat);
        const i64 right =
            broadcast_flat_offset(right_shape, right_strides, right_offset, result_shape, flat);
        out.push_back(op(left_data[static_cast<std::size_t>(left)],
                         right_data[static_cast<std::size_t>(right)]));
    }
    return out;
}

template <class Data, class Shape, class Strides>
Data binary_add_data(const Data& left_data, const Shape& left_shape_ref,
                     const Strides& left_strides_ref, i64 left_offset, const Data& right_data,
                     const Shape& right_shape_ref, const Strides& right_strides_ref,
                     i64 right_offset) {
    return binary_data(left_data, left_shape_ref, left_strides_ref, left_offset, right_data,
                       right_shape_ref, right_strides_ref, right_offset,
                       [](const auto& left, const auto& right) { return left + right; });
}

template <class Data, class Shape, class Strides>
Data binary_sub_data(const Data& left_data, const Shape& left_shape_ref,
                     const Strides& left_strides_ref, i64 left_offset, const Data& right_data,
                     const Shape& right_shape_ref, const Strides& right_strides_ref,
                     i64 right_offset) {
    return binary_data(left_data, left_shape_ref, left_strides_ref, left_offset, right_data,
                       right_shape_ref, right_strides_ref, right_offset,
                       [](const auto& left, const auto& right) { return left - right; });
}

template <class Data, class Shape, class Strides>
Data binary_mul_data(const Data& left_data, const Shape& left_shape_ref,
                     const Strides& left_strides_ref, i64 left_offset, const Data& right_data,
                     const Shape& right_shape_ref, const Strides& right_strides_ref,
                     i64 right_offset) {
    return binary_data(left_data, left_shape_ref, left_strides_ref, left_offset, right_data,
                       right_shape_ref, right_strides_ref, right_offset,
                       [](const auto& left, const auto& right) { return left * right; });
}

template <class T> std::vector<i64> tensor_index_values(const T& value) {
    std::vector<i64> values;
    const std::vector<i64> shape = as_i64_shape(value.shape);
    const std::vector<i64> strides = as_i64_shape(value.strides);
    const i64 count = element_count(shape);
    values.reserve(static_cast<std::size_t>(count));
    for (i64 flat = 0; flat < count; ++flat) {
        const i64 source = flat_offset(shape, strides, static_cast<i64>(value.offset), flat);
        values.push_back(static_cast<i64>(value.data[static_cast<std::size_t>(source)]));
    }
    return values;
}

template <class T> std::vector<i64> mask_index_values(const T& value) {
    std::vector<i64> values;
    for (std::size_t index = 0; index < value.data.size(); ++index) {
        if (value.data[index]) {
            values.push_back(static_cast<i64>(index));
        }
    }
    return values;
}

template <class T> IndexItem make_index_item(const T& value) {
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
    } else if constexpr (MaskIndexLike<Value>) {
        auto values = mask_index_values(value);
        return {.kind = IndexKind::Mask,
                .shape = {static_cast<i64>(values.size())},
                .values = std::move(values)};
    } else if constexpr (ShapedIndexLike<Value>) {
        return {.kind = IndexKind::Tensor,
                .shape = as_i64_shape(value.shape),
                .values = tensor_index_values(value)};
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

inline std::vector<i64> slice_values(const IndexItem& item, i64 extent) {
    const i64 step = item.has_step ? item.step : 1;
    std::vector<i64> values;
    if (step == 0 || extent <= 0) {
        return values;
    }
    i64 start = item.has_start ? item.start : (step < 0 ? extent - 1 : 0);
    i64 end = item.has_end ? item.end : (step < 0 ? -1 : extent);
    if (start < 0) {
        start += extent;
    }
    if (end < 0 && item.has_end) {
        end += extent;
    }
    if (step > 0) {
        start = std::clamp(start, i64{0}, extent);
        end = std::clamp(end, i64{0}, extent);
        for (i64 index = start; index < end; index += step) {
            values.push_back(index);
        }
        return values;
    }
    start = std::clamp(start, i64{-1}, extent - 1);
    end = std::clamp(end, i64{-1}, extent - 1);
    for (i64 index = start; index > end; index += step) {
        values.push_back(index);
    }
    return values;
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
                             const std::vector<i64>& strides, std::size_t& axis,
                             std::vector<OffsetContribution>& contributions) {
    if (axis >= shape.size()) {
        return;
    }
    OffsetContribution contribution;
    contribution.axis = axis;
    contribution.kind = ContributionKind::Slice;
    contribution.result_dim = out.shape.size();
    contribution.result_rank = 1;
    for (i64 index = 0; index < shape[axis]; ++index) {
        contribution.values.push_back(index);
    }
    out.shape.push_back(shape[axis]);
    out.strides.push_back(strides[axis]);
    contributions.push_back(std::move(contribution));
    ++axis;
}

inline std::vector<i64> unravel_index(const std::vector<i64>& shape, i64 flat) {
    std::vector<i64> coords(shape.size(), 0);
    for (std::size_t dim = shape.size(); dim > 0; --dim) {
        const std::size_t axis = dim - 1;
        const i64 extent = shape[axis];
        coords[axis] = extent == 0 ? 0 : flat % extent;
        flat = extent == 0 ? 0 : flat / extent;
    }
    return coords;
}

inline i64 contribution_index(const OffsetContribution& contribution,
                              const std::vector<i64>& result_shape,
                              const std::vector<i64>& result_coords) {
    if (contribution.result_rank == 0) {
        return 0;
    }
    i64 flat = 0;
    for (std::size_t index = 0; index < contribution.result_rank; ++index) {
        const std::size_t dim = contribution.result_dim + index;
        flat *= result_shape[dim];
        flat += result_coords[dim];
    }
    return flat;
}

inline void build_explicit_offsets(IndexPlan& out, const std::vector<i64>& shape,
                                   const std::vector<i64>& strides,
                                   const std::vector<OffsetContribution>& contributions) {
    const i64 total = element_count(out.shape);
    out.explicit_offsets.reserve(static_cast<std::size_t>(total));
    for (i64 flat = 0; flat < total; ++flat) {
        const std::vector<i64> coords = unravel_index(out.shape, flat);
        i64 source = out.offset;
        for (const OffsetContribution& contribution : contributions) {
            i64 coord = contribution.scalar;
            if (contribution.kind != ContributionKind::Scalar) {
                const i64 local = contribution_index(contribution, out.shape, coords);
                if (!contribution.values.empty()) {
                    coord = contribution.values[static_cast<std::size_t>(
                        local % static_cast<i64>(contribution.values.size()))];
                }
            }
            source +=
                normalize_scalar(coord, shape[contribution.axis]) * strides[contribution.axis];
        }
        out.explicit_offsets.push_back(source);
    }
    out.offset = out.explicit_offsets.empty() ? out.offset : out.explicit_offsets.front();
    out.strides = contiguous_strides(out.shape);
}

inline IndexPlan normalize_index(const std::vector<i64>& shape, const std::vector<i64>& strides,
                                 i64 offset, std::vector<IndexItem> items, bool cartesian) {
    items = expand_ellipsis(std::move(items), shape);
    IndexPlan out;
    out.offset = offset;
    std::vector<OffsetContribution> contributions;
    std::size_t axis = 0;
    bool advanced_shape_added = false;
    std::size_t direct_advanced_dim = 0;
    std::size_t direct_advanced_rank = 0;
    bool needs_explicit_offsets = false;
    for (const IndexItem& item : items) {
        switch (item.kind) {
        case IndexKind::Scalar: {
            if (axis >= shape.size()) {
                break;
            }
            contributions.push_back({.axis = axis,
                                     .kind = ContributionKind::Scalar,
                                     .scalar = normalize_scalar(item.scalar, shape[axis])});
            out.offset += normalize_scalar(item.scalar, shape[axis]) * strides[axis];
            ++axis;
            break;
        }
        case IndexKind::Slice: {
            if (axis >= shape.size()) {
                break;
            }
            const std::vector<i64> values = slice_values(item, shape[axis]);
            if (!values.empty()) {
                out.offset += values.front() * strides[axis];
            }
            OffsetContribution contribution;
            contribution.axis = axis;
            contribution.kind = ContributionKind::Slice;
            contribution.values = values;
            contribution.result_dim = out.shape.size();
            contribution.result_rank = 1;
            out.shape.push_back(static_cast<i64>(contribution.values.size()));
            out.strides.push_back(strides[axis] * (item.has_step ? item.step : 1));
            contributions.push_back(std::move(contribution));
            ++axis;
            break;
        }
        case IndexKind::NewAxis:
            out.shape.push_back(1);
            out.strides.push_back(0);
            break;
        case IndexKind::Mask:
            if (axis < shape.size()) {
                OffsetContribution contribution;
                contribution.axis = axis;
                contribution.kind = ContributionKind::Advanced;
                contribution.values = item.values;
                if (cartesian || !advanced_shape_added) {
                    contribution.result_dim = out.shape.size();
                    contribution.result_rank = item.shape.size();
                    out.shape.insert(out.shape.end(), item.shape.begin(), item.shape.end());
                    out.strides.insert(out.strides.end(), item.shape.size(), 1);
                    if (!cartesian) {
                        direct_advanced_dim = contribution.result_dim;
                        direct_advanced_rank = contribution.result_rank;
                    }
                    advanced_shape_added = true;
                } else {
                    contribution.result_dim = direct_advanced_dim;
                    contribution.result_rank = direct_advanced_rank;
                }
                contributions.push_back(std::move(contribution));
                needs_explicit_offsets = true;
                ++axis;
            }
            break;
        case IndexKind::Tensor:
            if (axis < shape.size()) {
                OffsetContribution contribution;
                contribution.axis = axis;
                contribution.kind = ContributionKind::Advanced;
                contribution.values = item.values;
                if (cartesian || !advanced_shape_added) {
                    contribution.result_dim = out.shape.size();
                    contribution.result_rank = item.shape.size();
                    out.shape.insert(out.shape.end(), item.shape.begin(), item.shape.end());
                    out.strides.insert(out.strides.end(), item.shape.size(), 1);
                    if (!cartesian) {
                        direct_advanced_dim = contribution.result_dim;
                        direct_advanced_rank = contribution.result_rank;
                    }
                    advanced_shape_added = true;
                } else {
                    contribution.result_dim = direct_advanced_dim;
                    contribution.result_rank = direct_advanced_rank;
                }
                contributions.push_back(std::move(contribution));
                needs_explicit_offsets = true;
                ++axis;
            }
            break;
        case IndexKind::Ellipsis:
            break;
        }
    }
    while (axis < shape.size()) {
        append_full_axis(out, shape, strides, axis, contributions);
    }
    if (needs_explicit_offsets) {
        build_explicit_offsets(out, shape, strides, contributions);
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

template <class Data> Data result_data(const Data& data, const IndexPlan& plan) {
    if (plan.explicit_offsets.empty()) {
        return data;
    }
    Data out;
    out.reserve(plan.explicit_offsets.size());
    for (const i64 source : plan.explicit_offsets) {
        out.push_back(data[static_cast<std::size_t>(source)]);
    }
    return out;
}

inline std::vector<i64> result_strides(const IndexPlan& plan) {
    return plan.explicit_offsets.empty() ? plan.strides : contiguous_strides(plan.shape);
}

inline i64 result_offset(const IndexPlan& plan) {
    return plan.explicit_offsets.empty() ? plan.offset : 0;
}

template <class Data, class Shape, class Strides, class T, class... Idx>
void assign_scalar(Data& data, const Shape& shape, const Strides& strides, i64 offset,
                   const T& value, const Idx&... idx) {
    const IndexPlan plan = index_plan(shape, strides, offset, idx...);
    const i64 total = element_count(plan.shape);
    for (i64 flat = 0; flat < total; ++flat) {
        const i64 dest = plan.explicit_offsets.empty()
                             ? flat_offset(plan.shape, plan.strides, plan.offset, flat)
                             : plan.explicit_offsets[static_cast<std::size_t>(flat)];
        data[static_cast<std::size_t>(dest)] = value;
    }
}

template <class DestData, class DestShape, class DestStrides, class SrcData, class SrcShape,
          class SrcStrides, class... Idx>
void assign_tensor(DestData& dest_data, const DestShape& dest_shape,
                   const DestStrides& dest_strides, i64 dest_offset, const SrcData& src_data,
                   const SrcShape& src_shape, const SrcStrides& src_strides, i64 src_offset,
                   const Idx&... idx) {
    const IndexPlan dest = index_plan(dest_shape, dest_strides, dest_offset, idx...);
    const std::vector<i64> source_shape = as_i64_shape(src_shape);
    const std::vector<i64> source_strides = as_i64_shape(src_strides);
    if (!broadcast_compatible(source_shape, dest.shape)) {
        throw std::runtime_error("incompatible tensor assignment shape");
    }
    const i64 dest_count = element_count(dest.shape);
    for (i64 flat = 0; flat < dest_count; ++flat) {
        const i64 dest_offset_value = dest.explicit_offsets.empty()
                                          ? flat_offset(dest.shape, dest.strides, dest.offset, flat)
                                          : dest.explicit_offsets[static_cast<std::size_t>(flat)];
        dest_data[static_cast<std::size_t>(dest_offset_value)] = src_data[static_cast<std::size_t>(
            broadcast_flat_offset(source_shape, source_strides, src_offset, dest.shape, flat))];
    }
}

} // namespace ndad
