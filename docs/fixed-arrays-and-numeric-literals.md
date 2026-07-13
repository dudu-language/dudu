# Fixed Arrays And Numeric Literals

`array[T][shape]` is fixed-size contiguous storage. It lowers to nested
`std::array` storage and its extents are compile-time values.

See [Arrays, Views, And Indexing](arrays-views-and-indexing.md) for
multidimensional indexing, slicing, ownership, strides, and library operators.

```python
samples: array[f32][1024]
transform: array[f32][4, 4]
volume: array[u8][64, 64, 32]
```

## Inferring Shape From A Literal

The storage family and element type remain explicit. A non-empty rectangular
initializer can supply every extent:

```python
kernel: array[f32] = [
    [1.0, 0.0, -1.0],
    [1.0, 0.0, -1.0],
    [1.0, 0.0, -1.0],
]
# effective type: array[f32][3, 3]
```

Inference is rank-independent:

```python
line: array[i32] = [1, 2, 3]                 # array[i32][3]
cube: array[u8] = [[[1, 2]], [[3, 4]]]       # array[u8][2, 1, 2]
volume: array[i64] = [
    [[[1, 2]]],
    [[[3, 4]]],
]                                               # array[i64][2, 1, 1, 2]
```

A bare literal chooses a dynamic `list`, not fixed storage:

```python
dynamic = [[1, 2], [3, 4]]                   # list[list[i32]]
fixed: array[i32] = [[1, 2], [3, 4]]         # array[i32][2, 2]
```

## Explicit Shape Checking

When extents are written, the initializer must match them:

```python
matrix: array[i32][2, 2] = [
    [1, 2],
    [3, 4],
]
```

A rectangular but incorrect shape reports both shapes and points at the first
mismatching extent:

```python
matrix: array[i32][2, 2] = [
    [1, 2, 3],
    [4, 5, 6],
]
# error: array literal shape mismatch: expected [2, 2], got [2, 3]
```

A ragged literal points at the first row whose shape differs:

```python
matrix: array[i32] = [
    [1, 2],
    [3],
]
# error: ragged array literal
```

The root empty literal cannot determine a shape:

```python
values: array[i32] = []
# error: array shape cannot be inferred from an empty literal

values: array[i32][0]
```

Uniform zero inner extents remain inferable, so `[[], []]` has shape `[2, 0]`.

## Numeric Literal Widths

Unconstrained integer literals default to `i32`. Unconstrained decimal
literals default to `f64`:

```python
count = 3       # i32
ratio = 0.5     # f64
```

Expected numeric context selects the required width without repeated casts:

```python
weights: array[f32] = [
    [0.25, 0.5],
    [0.75, 1.0],
]
```

Every scalar above is checked as `f32` because the array element type supplies
that context. The same rule applies to annotated locals, function arguments,
returns, and collection elements.

Numeric conversions remain checked. Contextual literal conversion is not a
general implicit conversion between already typed runtime values.

## Editor And C++ Output

Hover uses the effective shaped type. When source contains a partial annotation
such as `array[f32]`, the inlay hint appends the inferred extent suffix, making
the displayed type `array[f32][rows, columns, ...]`.

Generated C++ preserves the same rank and extents:

```text
array[f32][2, 3]
std::array<std::array<float, 3>, 2>
```

Shape inference is completed before C++ emission. Code generation does not
re-guess rank or extents from source text.
