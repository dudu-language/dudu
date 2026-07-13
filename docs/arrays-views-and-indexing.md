<a id="arrays-views-and-indexing"></a>
# Arrays, Views, And Indexing

[Dudu manual](https://dudulang.org/docs.html#containers) | Previous: [Allocation and lifetimes](allocation-and-lifetimes.md) | Next: [Known limitations](known-limitations.md)

Dudu gives fixed arrays Python-shaped multidimensional indexing. The language
defines fixed-array storage and non-owning array views. Numeric libraries can
use the same bracket syntax for tensors, masks, gathers, GPU buffers, or other
storage without adding library names to the compiler.

## Fixed Arrays

`array[T][extents]` is fixed-size, contiguous, row-major storage:

```python
line: array[i32][4]
matrix: array[f32][3, 4]
image: array[u8][480, 640, 4]
```

Rank is the number of extents. The examples have ranks 1, 2, and 3. Every
extent is part of the type and must be known at compile time.

A rectangular literal can infer the extents:

```python
matrix: array[i32] = [
    [1, 2, 3],
    [4, 5, 6],
]
# effective type: array[i32][2, 3]
```

The element type remains explicit because `[1, 2]` alone is a dynamic
`list[i32]`. See [Fixed Arrays And Numeric Literals](fixed-arrays-and-numeric-literals.md)
for literal inference and mismatch diagnostics.

## Row-Major Layout

For this matrix:

```text
shape   [2, 3]
strides [3, 1]

        column
          0   1   2
row 0     1   2   3
row 1     4   5   6

flat      1   2   3   4   5   6
```

The last axis is adjacent in memory. Moving one row advances three elements;
moving one column advances one element.

## Scalar Indexing

Each scalar index consumes one source axis:

```python
image: array[u8][480, 640, 4]

row = image[10]          # array[u8][640, 4]
pixel = image[10, 20]    # array[u8][4]
red = image[10, 20, 0]  # u8
```

Assigning a lower-rank subarray to an owning `array` value copies it. Use a
slice when the result should share the source storage.

Fixed-array scalar indexing follows native unchecked indexing. It does not
normalize negative scalar indices. A numeric library may define checked or
negative scalar indexing in its own `@operator("[]")` implementation.

## Slices

A slice consumes one axis and preserves it in the result:

| Syntax | Meaning |
| --- | --- |
| `:` | whole axis |
| `start:stop` | half-open range |
| `start:` | from `start` to the end |
| `:stop` | from the beginning to `stop` |
| `start:stop:step` | half-open range with a positive step |
| `::step` | whole axis with a positive step |

Examples:

```python
matrix: array[i32][3, 4]

row = matrix[1, :]           # array_view[i32][4]
column = matrix[:, 1]        # array_view[i32][3]
patch = matrix[1:3, 2:4]     # array_view[i32][2, 2]
every_other = matrix[:, ::2] # array_view[i32][3, 2]
```

Slice bounds are clipped to the source extent. Negative slice bounds count
from the end:

```python
values: array[i32] = [1, 2, 3, 4]

tail = values[-3:]  # [2, 3, 4]
wide = values[1:99] # [2, 3, 4]
```

Built-in fixed-array slices require a positive step. A literal zero or
negative step is a compile error. A runtime step that is not positive is a
runtime error. Tensor libraries may define a broader policy in their own
indexing operators.

## Ellipsis

`...` fills the source axes not consumed by the other index items. At most one
ellipsis may appear in an index:

```python
volume: array[f32][2, 3, 4, 5]

last_axis = volume[..., 2]     # array_view[f32][2, 3, 4]
middle = volume[1, ..., 0:2]   # array_view[f32][3, 4, 2]
same = volume[...]             # array_view[f32][2, 3, 4, 5]
```

Axes omitted after the final index item are preserved too. `volume[1]` and
`volume[1, ...]` therefore address the same source axes, though the first is a
subarray expression and the second is a view.

## New Axes

`None` inside an index adds a size-one axis without consuming a source axis:

```python
bias: array[f32][4]

row_bias = bias[None, :]       # array_view[f32][1, 4]
column_bias = bias[:, None]    # array_view[f32][4, 1]
framed = bias[None, :, None]   # array_view[f32][1, 4, 1]
```

This is useful when a tensor library implements broadcasting. The built-in
array view only changes shape and strides; it does not perform arithmetic or
broadcasting by itself.

## Predicting Result Shapes

Process index items from left to right:

| Index item | Source axes consumed | Result axes added |
| --- | ---: | ---: |
| scalar integer | 1 | 0 |
| slice | 1 | 1, with the sliced extent |
| `None` | 0 | 1, with extent 1 |
| `...` | all otherwise-unmatched axes | the same axes |

Any untouched trailing source axes are appended.

For `image: array[u8][2, 3, 4]`:

| Expression | Result | Calculation |
| --- | --- | --- |
| `image[1, 2, 3]` | `u8` | three scalar indices remove three axes |
| `image[1, :, 2]` | `array_view[u8][3]` | scalar, preserved slice, scalar |
| `image[:, :, 0]` | `array_view[u8][2, 3]` | two slices, then scalar |
| `image[0:2, 1:3, :]` | `array_view[u8][2, 2, 4]` | three preserved slices |
| `image[None, ..., 2]` | `array_view[u8][1, 2, 3]` | new axis, two filled axes, scalar |

Literal bounds produce literal result extents. Runtime bounds produce `dyn` for
only the affected result axis:

```python
def choose(values: &array[i32][16], start: i32) -> array_view[i32][dyn]:
    return values[start:]
```

## Views And Ownership

Any fixed-array index containing a slice, ellipsis, or new axis returns
`array_view[T][shape]`. An array view stores:

- a pointer to the source elements
- result shape
- strides
- an offset into the source

It does not own storage. The source must outlive the view.

Views share their source:

```python
matrix: array[i32] = [
    [1, 2],
    [3, 4],
]

column = matrix[:, 0]
column[1] = 30

assert matrix[1, 0] == 30
```

Dudu diagnoses direct local-address escapes, but it does not provide Rust-style
lifetime proof for every view. Do not return a view into a local fixed array.
Returning a view is valid when the referenced owner outlives the result by an
API or object-lifetime contract.

## Strides And Contiguity

A view can be contiguous or strided:

| View | Contiguous? |
| --- | --- |
| full fixed array | yes |
| complete trailing rows | often yes |
| one matrix row | yes |
| one matrix column | no |
| positive step greater than one | no |
| inserted `None` axis | shares storage; its added stride is zero |

`array_view` does not promise contiguity in its type. Iteration and `len(view)`
follow logical flattened view order and honor strides.

## Copying And Materialization

Slicing does not copy. Copy into an owning value explicitly when independent
storage is required:

```python
def copy_four(source: array_view[i32][4]) -> array[i32][4]:
    out: array[i32][4]
    for i in range(4):
        out[i] = source[i]
    return out
```

The language does not silently materialize a noncontiguous view for a native
API. A tensor library may provide explicit operations such as `copy()`,
`contiguous()`, `to_tensor()`, or a device transfer.

## Indexed Assignment

Scalar fixed-array and view indexing is assignable:

```python
matrix[1, 2] = 42
column = matrix[:, 2]
column[0] += 1
```

Whole-slice assignment, broadcast assignment, masks, repeated-index scatter,
and atomic accumulation are library operations. They are not built-in
fixed-array policies.

## Library-Defined Indexing

Classes define bracket behavior with visible operators:

```python
class Grid:
    @operator("[]")
    def at(self, row: i32, col: i32) -> Cell:
        return self.cells[(row * self.width) + col]

    @operator("[]=")
    def set_at(self, row: i32, col: i32, value: Cell):
        self.cells[(row * self.width) + col] = value
```

An arbitrary-rank library uses a typed parameter pack rather than one overload
per rank:

```python
class Tensor[T]:
    @operator("[]")
    def at[Idx...](self, *idx: Idx) -> TensorSelection[T, Idx...]:
        return self.select(idx...)

    @operator("[]=")
    def set_at[Idx...](self, *idx: Idx, value: TensorAssignable[T, Idx...]):
        self.assign(idx..., value)
```

Each item keeps its type. `:` is `slice`, `...` is `ellipsis`, index-position
`None` is `new_axis`, and a mask or index tensor keeps its library-defined
type. A library can overload a `*idx: basic_index` path for view-safe scalar,
slice, ellipsis, and new-axis indexing, then use a generic `Idx...` path for
materializing gathers.

The compiler does not decide whether these operations return views or copies:

```python
train_x = x[mask, :]                 # library policy
correct = logits[rows, labels]       # library policy
tokens = embeddings[token_ids, :]    # library policy
weights[mask, :] = 0.0               # library policy
```

The checked `ndad` target package under
[`tests/targets/tensor_indexing`](../tests/targets/tensor_indexing) proves mask,
gather, scatter, broadcast, arbitrary-rank, view/copy, and runtime-shape APIs.
It is a compiler validation package, not a tensor library shipped in Dudu's
standard surface.

## Static And Runtime Shapes

Fixed `array` extents are compile-time values. A library tensor can use runtime
shape storage and optionally add shaped type metadata:

```python
runtime: Tensor[f32]
partly_known: Tensor[f32][dyn, 784]
known: Tensor[f32][32, 784]
```

`dyn` means that one extent is known only at runtime. Static shape metadata is
optional, but it can catch incompatible matrix pipelines, buffers, and kernel
contracts before execution. It does not force every tensor operation into the
type system.

## Native Handoff

A contiguous fixed array can be passed to C or C++ through an element pointer
and count:

```python
samples: array[f32][1024]
native_process(&samples[0], len(samples))
```

Do not pass a general `array_view` as a contiguous pointer without checking its
strides. Numeric libraries should expose explicit span, descriptor, BLAS, or
GPU-buffer conversion APIs that preserve shape, stride, device, and lifetime
information.

## Diagnostics

Dudu reports indexing errors at the relevant expression, including:

- too many indices for a fixed array
- more than one ellipsis
- a non-positive built-in slice step
- a shaped view assigned to an incompatible rank or extent
- a standalone slice outside brackets
- a class indexed without a matching `@operator("[]")`
- indexed assignment without a matching `@operator("[]=")`
- rejected operator candidates with per-argument type reasons

For example:

```text
cannot assign array_view[i32][3] to array_view[i32][4] without an explicit cast;
shape mismatch: expected [4], got [3] (axis 0 expected 4, got 3)
```

## Tested Examples

- [`array_indexing_tutorial.dd`](../tests/fixtures/array_indexing_tutorial.dd)
  executes scalar, slice, step, ellipsis, new-axis, reslice, clipping, and
  shared-mutation behavior.
- [`fixed_array_python_slices.dd`](../tests/targets/tensor_indexing/fixed_array_python_slices.dd)
  exercises the same generic view builder on rank-3 data.
- [`library_variadic_hooks.dd`](../tests/targets/tensor_indexing/library_variadic_hooks.dd)
  validates arbitrary-rank typed library dispatch.
- [`library_index_category_hooks.dd`](../tests/targets/tensor_indexing/library_index_category_hooks.dd)
  validates basic-index and advanced-index overload selection.
- [`ndad_view_copy_runtime.dd`](../tests/targets/tensor_indexing/ndad_view_copy_runtime.dd)
  distinguishes shared views from owning gathered results.

The LSP test suite checks inferred view shapes, hover text, bad-shape
diagnostics, and navigation from an indexed expression to its selected
`@operator("[]")` declaration.

## Limits

- Built-in fixed arrays provide basic scalar/slice/new-axis/ellipsis indexing;
  masks, gathers, broadcasting, devices, and autograd are library behavior.
- A view does not own storage and must not outlive its source.
- Static result extents are reported only when the compiler can prove them;
  runtime-shaped libraries retain dynamic shape metadata in their values.
- Dudu does not bundle NumPy, PyTorch, BLAS, OpenCL, CUDA, or a production
  tensor library.
