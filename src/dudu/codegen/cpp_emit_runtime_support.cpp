#include "dudu/codegen/cpp_emit_runtime_support.hpp"

namespace dudu {

void emit_result_runtime_support(std::ostringstream& out) {
    out << R"cpp(template <typename T> struct OkValue { T value; };
template <typename E> struct ErrValue { E err; };
template <typename T> OkValue<T> Ok(T value) { return {std::move(value)}; }
template <typename E> ErrValue<E> Err(E err) { return {std::move(err)}; }
template <typename T, typename E> struct Result {
    bool ok{};
    std::variant<T, E> storage;
    Result(OkValue<T> ok_value)
        : ok(true), storage(std::in_place_index<0>, std::move(ok_value.value)) {}
    Result(ErrValue<E> err_value)
        : ok(false), storage(std::in_place_index<1>, std::move(err_value.err)) {}
    T& value_ref() { return std::get<0>(storage); }
    const T& value_ref() const { return std::get<0>(storage); }
    E& error_ref() { return std::get<1>(storage); }
    const E& error_ref() const { return std::get<1>(storage); }
};
)cpp";
}

void emit_print_runtime_support(std::ostringstream& out) {
    out << "template <typename T> void print(const T& value) { std::cout << value << '\\n'; }\n";
}

void emit_tuple_runtime_support(std::ostringstream& out) {
    out << R"cpp(template <typename T0> struct Tuple1 { T0 _0{}; };
template <typename T0, typename T1> struct Tuple2 { T0 _0{}; T1 _1{}; };
template <typename T0, typename T1, typename T2> struct Tuple3 { T0 _0{}; T1 _1{}; T2 _2{}; };
template <typename T0, typename T1, typename T2, typename T3> struct Tuple4 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; };
template <typename T0, typename T1, typename T2, typename T3, typename T4> struct Tuple5 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; T4 _4{}; };
template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5> struct Tuple6 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; T4 _4{}; T5 _5{}; };
template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6> struct Tuple7 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; T4 _4{}; T5 _5{}; T6 _6{}; };
template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7> struct Tuple8 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; T4 _4{}; T5 _5{}; T6 _6{}; T7 _7{}; };
)cpp";
}

void emit_index_runtime_support(std::ostringstream& out) {
    out << R"cpp(struct Slice {
    bool has_start{};
    bool has_end{};
    bool has_step{};
    int64_t start{};
    int64_t end{};
    int64_t step{1};
};
struct Ellipsis {};
struct NewAxis {};
#ifndef DUDU_INDEX_CATEGORY_TYPES
#define DUDU_INDEX_CATEGORY_TYPES
struct ScalarIndex {
    int64_t value{};
    template <typename T, typename = std::enable_if_t<std::is_integral_v<std::decay_t<T>>>>
    ScalarIndex(T v) : value(static_cast<int64_t>(v)) {}
};
enum class BasicIndexKind { Scalar, Slice, Ellipsis, NewAxis };
struct BasicIndex {
    BasicIndexKind kind{BasicIndexKind::Scalar};
    int64_t scalar{};
    Slice slice{};
    BasicIndex(ScalarIndex value) : kind(BasicIndexKind::Scalar), scalar(value.value) {}
    template <typename T, typename = std::enable_if_t<std::is_integral_v<std::decay_t<T>>>>
    BasicIndex(T value) : kind(BasicIndexKind::Scalar), scalar(static_cast<int64_t>(value)) {}
    BasicIndex(Slice value) : kind(BasicIndexKind::Slice), slice(value) {}
    BasicIndex(Ellipsis) : kind(BasicIndexKind::Ellipsis) {}
    BasicIndex(NewAxis) : kind(BasicIndexKind::NewAxis) {}
};
#endif
enum class SliceSpecKind { Index, Range, Ellipsis, NewAxis };
struct SliceSpec {
    SliceSpecKind kind{SliceSpecKind::Index};
    int64_t index{};
    Slice slice{};
    static SliceSpec at(int64_t value) { return {SliceSpecKind::Index, value, {}}; }
    static SliceSpec range(Slice value) { return {SliceSpecKind::Range, 0, value}; }
    static SliceSpec ellipsis() { return {SliceSpecKind::Ellipsis, 0, {}}; }
    static SliceSpec new_axis() { return {SliceSpecKind::NewAxis, 0, {}}; }
};
)cpp";
}

void emit_array_view_runtime_support(std::ostringstream& out) {
    out << R"cpp(template <typename T> struct ArrayView {
    T* data{};
    std::vector<std::size_t> shape{};
    std::vector<std::size_t> strides{};
    std::size_t offset{};
    std::size_t size() const {
        std::size_t total = 1;
        for (std::size_t dim : shape) { total *= dim; }
        return shape.empty() ? 0 : total;
    }
    T& operator[](std::size_t flat) const {
        std::size_t source = offset;
        for (std::size_t dim = shape.size(); dim > 0; --dim) {
            const std::size_t axis = dim - 1;
            const std::size_t extent = shape[axis];
            const std::size_t coord = extent == 0 ? 0 : flat % extent;
            flat = extent == 0 ? 0 : flat / extent;
            source += coord * strides[axis];
        }
        return data[source];
    }
    T& at(std::initializer_list<std::size_t> coords) const {
        std::size_t source = offset;
        std::size_t axis = 0;
        for (std::size_t coord : coords) {
            source += coord * strides[axis];
            ++axis;
        }
        return data[source];
    }
    struct Iterator {
        const ArrayView* view{};
        std::size_t index{};
        T& operator*() const { return (*view)[index]; }
        Iterator& operator++() { ++index; return *this; }
        bool operator!=(const Iterator& other) const { return index != other.index; }
    };
    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, size()}; }
};
template <typename T> struct ArrayTraits {
    using Element = T;
    static void shape(std::vector<std::size_t>&) {}
};
template <typename T, std::size_t N> struct ArrayTraits<std::array<T, N>> {
    using Element = typename ArrayTraits<T>::Element;
    static void shape(std::vector<std::size_t>& out) {
        out.push_back(N);
        ArrayTraits<T>::shape(out);
    }
};
template <typename T> T* array_data(T& value) { return &value; }
template <typename T, std::size_t N> auto array_data(std::array<T, N>& value) {
    return array_data(value[0]);
}
inline std::vector<std::size_t> contiguous_strides(const std::vector<std::size_t>& shape) {
    std::vector<std::size_t> strides(shape.size(), 1);
    for (std::size_t dim = shape.size(); dim > 1; --dim) {
        strides[dim - 2] = strides[dim - 1] * shape[dim - 1];
    }
    return strides;
}
template <typename Array> auto make_array_view(Array& value) {
    using Element = typename ArrayTraits<Array>::Element;
    std::vector<std::size_t> shape;
    ArrayTraits<Array>::shape(shape);
    return ArrayView<Element>{array_data(value), shape, contiguous_strides(shape), 0};
}
inline void append_full_axis(auto& out, const auto& view, std::size_t& axis) {
    out.shape.push_back(view.shape[axis]);
    out.strides.push_back(view.strides[axis]);
    ++axis;
}
template <typename T> ArrayView<T> array_view_slice(
    ArrayView<T> view, std::initializer_list<SliceSpec> specs) {
    ArrayView<T> out{view.data, {}, {}, view.offset};
    std::size_t consumed = 0;
    for (const SliceSpec& spec : specs) {
        if (spec.kind != SliceSpecKind::Ellipsis && spec.kind != SliceSpecKind::NewAxis) {
            ++consumed;
        }
    }
    const std::size_t ellipsis_fill =
        view.shape.size() > consumed ? view.shape.size() - consumed : 0;
    std::size_t axis = 0;
    for (const SliceSpec& spec : specs) {
        if (spec.kind == SliceSpecKind::NewAxis) {
            out.shape.push_back(1);
            out.strides.push_back(0);
            continue;
        }
        if (spec.kind == SliceSpecKind::Ellipsis) {
            for (std::size_t i = 0; i < ellipsis_fill && axis < view.shape.size(); ++i) {
                append_full_axis(out, view, axis);
            }
            continue;
        }
        if (axis >= view.shape.size()) { break; }
        if (spec.kind == SliceSpecKind::Index) {
            out.offset += static_cast<std::size_t>(spec.index) * view.strides[axis];
        } else {
            const std::int64_t extent = static_cast<std::int64_t>(view.shape[axis]);
            const auto bound = [extent](std::int64_t value) {
                if (value < 0) { value += extent; }
                return std::clamp<std::int64_t>(value, 0, extent);
            };
            const std::int64_t raw_step = spec.slice.has_step ? spec.slice.step : 1;
            if (raw_step <= 0) {
                throw std::invalid_argument("array slice step must be positive");
            }
            const std::size_t start = static_cast<std::size_t>(
                spec.slice.has_start ? bound(spec.slice.start) : 0);
            const std::size_t end = static_cast<std::size_t>(
                spec.slice.has_end ? bound(spec.slice.end) : extent);
            const std::size_t step = static_cast<std::size_t>(raw_step);
            out.offset += start * view.strides[axis];
            out.shape.push_back(end <= start ? 0 : ((end - start) + step - 1) / step);
            out.strides.push_back(view.strides[axis] * step);
        }
        ++axis;
    }
    while (axis < view.shape.size()) {
        append_full_axis(out, view, axis);
    }
    return out;
}
template <typename Array> auto array_view_slice(
    Array& value, std::initializer_list<SliceSpec> specs) {
    return array_view_slice(make_array_view(value), specs);
}
)cpp";
}

void emit_strided_span_runtime_support(std::ostringstream& out) {
    out << R"cpp(template <typename T> struct StridedSpan {
    T* data{};
    std::size_t count{};
    std::size_t stride{};
    T& operator[](std::size_t index) const { return data[index * stride]; }
    std::size_t size() const { return count; }
    struct Iterator {
        T* current{};
        std::size_t stride{};
        T& operator*() const { return *current; }
        Iterator& operator++() { current += stride; return *this; }
        bool operator!=(const Iterator& other) const { return current != other.current; }
    };
    Iterator begin() const { return {data, stride}; }
    Iterator end() const { return {data + count * stride, stride}; }
};
)cpp";
}

void emit_shader_runtime_support(std::ostringstream& out) {
    out << R"cpp(namespace shader {
struct GlobalId { int32_t x{}; int32_t y{}; int32_t z{}; };
inline GlobalId global_id{};
} // namespace shader
)cpp";
}

} // namespace dudu
