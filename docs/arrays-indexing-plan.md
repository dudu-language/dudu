# Arrays, Matrix, Tensor, And Slicing Plan

Dudu should support Python-feeling indexing for serious numeric and graphics
code without hiding expensive copies or heap allocation.

## Goals

- Make vector, matrix, and tensor code pleasant.
- Support fixed-size arrays with clear type syntax.
- Support multi-dimensional indexing.
- Support slicing, but make copy-vs-view semantics explicit.
- Let libraries provide matrix/tensor storage and execution backends while
  reusing the same indexing syntax.
- Lower to readable C++ and interoperate with libraries such as glm, Eigen,
  OpenCV, spans, and raw C arrays.

## Type Syntax

Dynamic owning list:

```python
players: list[Player]
```

Fixed-size array:

```python
players: array[Player][5]
pixels: array[Color][240 * 160]
```

Fixed-size matrix/tensor:

```python
mat: array[f32][4, 4]
volume: array[f32][64, 64, 32]
```

The first bracket list is the element type. The second bracket list is the
shape. This avoids pointer/nesting ambiguity and keeps fixed arrays visibly
different from generic type arguments.

When a literal initializer gives an unambiguous shape, the shape can be inferred:

```python
xs: array[i32] = [1, 2, 3, 4]  # array[i32][4]

identity: array[f32] = [       # array[f32][4, 4]
    [1.0, 0.0, 0.0, 0.0],
    [0.0, 1.0, 0.0, 0.0],
    [0.0, 0.0, 1.0, 0.0],
    [0.0, 0.0, 0.0, 1.0],
]
```

Shape inference is only for declarations with an initializer that fully
determines the fixed shape. Without an initializer, write the shape explicitly:

```python
scratch: array[f32][4, 4]
```

Ragged literals are errors, and empty literals need an explicit shape.

Likely lowerings:

```text
array[T] = [...]  -> fixed contiguous storage with inferred shape
array[T][N]       -> std::array<T, N> or equivalent fixed contiguous storage
array[T][M, N]    -> dudu::Array2<T, M, N> or equivalent contiguous storage
array[T][A, B, C] -> dudu::ArrayN<T, A, B, C> or equivalent contiguous storage
list[T]       -> std::vector<T>
span[T]       -> std::span<T>
```

The exact C++ helper type can change, but the Dudu semantics should be stable:
fixed arrays own inline storage, lists own dynamic storage, and spans/views do
not own data.

Status: explicit `array[T][N]` and `array[T][M, N]` compile and lower to nested
`std::array` storage. `array[T] = literal` infers fixed shapes from non-empty
rectangular literals.

## Construction

Fixed arrays can be initialized with literals:

```python
xs: array[i32][4] = [1, 2, 3, 4]
ys: array[i32] = [1, 2, 3, 4]
```

Matrices can use nested literals:

```python
identity: array[f32][4, 4] = [
    [1.0, 0.0, 0.0, 0.0],
    [0.0, 1.0, 0.0, 0.0],
    [0.0, 0.0, 1.0, 0.0],
    [0.0, 0.0, 0.0, 1.0],
]
```

The explicit shape form checks the initializer shape. The inferred form reduces
typing when the initializer already carries the shape.

Dynamic arrays use `list[T]`:

```python
players: list[Player] = []
players.reserve(count)
```

Specialized allocation belongs to libraries and arenas, not core array syntax:

```python
players = arena.list[Player](count)
scratch = frame_allocator.list[f32](1024)
```

Those APIs should return normal library-owned values with RAII/destructor
cleanup where possible. Raw pointers remain available for low-level interop, but
they are not the main array story.

## Indexing

Single-dimensional indexing:

```python
value = xs[i]
xs[i] = value
```

Multi-dimensional indexing:

```python
value = mat[row, col]
mat[row, col] = value
voxel = volume[x, y, z]
```

Nested indexing should remain valid when the underlying type is actually nested:

```python
value = rows[y][x]
```

But fixed multidimensional arrays should prefer comma indexing because it maps
to one shape-aware object rather than nested containers.

Status: comma indexing lowers for Dudu-native fixed arrays, matching the current
nested `std::array` representation.

## Slicing

Python slicing is valuable for numeric code:

```python
row = mat[row_index, :]
col = mat[:, col_index]
patch = image[y0:y1, x0:x1]
every_other = samples[::2]
```

The key rule: slicing should produce a view by default where possible, not an
implicit owning copy.

Proposed spellings:

```python
row: span[f32] = mat[row_index, :]
patch: view[f32, 2] = image[y0:y1, x0:x1]
copy: list[f32] = list(row)
```

If a source container cannot produce a safe view, the compiler should require an
explicit copy:

```python
copy = list(items[a:b])
```

## Advanced Indexing

Useful target forms:

```python
rgb = image[y, x, 0:3]
channel = image[:, :, channel_index]
tile = image[y:y + 8, x:x + 8]
```

Do not add NumPy-style arbitrary gather/scatter indexing until normal slices,
views, and tensor shapes are solid.

## Swizzling

GLSL-style swizzling is valuable for vector math and shader-like code:

```python
v: Vec4[f32]
xy: Vec2[f32] = v.xy
rgb: Vec3[f32] = color.rgb
v.xy = Vec2[f32](1.0, 2.0)
```

Swizzles should be a compile-time feature for vector-like types, not arbitrary
dynamic field lookup. Supported component sets should be explicit:

- `x`, `y`, `z`, `w`
- `r`, `g`, `b`, `a`
- optionally `s`, `t`, `p`, `q`

Rules:

- read swizzles may repeat components: `v.xx`
- write swizzles may not repeat components: `v.xx = ...` is an error
- swizzle length determines the vector result type
- scalar one-component access remains normal member access when the type has the
  component
- libraries can opt vector types into swizzling through compile-time metadata or
  known component fields/operators

The compiler should not special-case one imported library such as glm. Native
Dudu vector types and imported vector wrappers should use the same generic
swizzle mechanism.

## Library-Defined Indexing

The language should define indexing and slicing syntax, but libraries should be
able to own the semantics for their types.

For Dudu-native arrays:

```python
mat: array[f32][4, 4]
value = mat[row, col]
patch = mat[0:2, 0:2]
```

For library tensor types:

```python
a: gpu.Tensor[f32, 2] = gpu.zeros[f32](rows, cols)
b: gpu.Tensor[f32, 2] = gpu.zeros[f32](rows, cols)

a[row, col] = 1.0
tile = a[y:y + 16, x:x + 16]
c = gpu.matmul(a, b)
```

The compiler should not need to know that `gpu.Tensor` is a GPU object. It
should lower indexing, slicing, and assignment through ordinary overloadable
hooks such as:

```python
class Tensor[T, Rank]:
    @operator("[]")
    def index(self, indices: IndexList) -> T:
        ...

    @operator("[]=")
    def set_index(self, indices: IndexList, value: T):
        ...

    def slice(self, ranges: SliceList) -> TensorView[T, Rank]:
        ...
```

The exact hook names can change. The key rule is that tensor libraries can map
Dudu syntax to CPU views, GPU buffer views, lazy expressions, kernel launches,
or backend calls without the core language embedding CUDA/Vulkan/OpenCL logic.

This also applies to imported C++ libraries if native header awareness can see
or adapt the relevant operators:

```python
import cpp "Eigen/Dense" as eigen

m: eigen.MatrixXf
value = m(row, col)        # direct C++ call stays valid
```

Dudu indexing syntax for imported types should be opt-in through wrappers or
recognized C++ operators. Do not special-case specific tensor libraries in the
compiler.

## Bounds Checks

Bounds behavior should be target/configurable:

- debug/checked mode: bounds checks on Dudu-native arrays, lists, spans, views
- release/native mode: allow unchecked lowering when configured
- imported C++ containers follow their own indexing semantics unless wrapped

The default should prefer catching mistakes while developing.

## Interop

Imported C++ matrix/vector libraries should continue to work directly:

```python
import cpp "glm/glm.hpp" as glm

a: glm.vec3
b: glm.vec3
d = glm.dot(a, b)
```

Dudu-native fixed arrays and views should be passable to C APIs through explicit
pointer/span helpers:

```python
pixels: array[Color][240 * 160]
upload_texture(&pixels[0], len(pixels))
```

## Target Examples

These examples are the suite we should make compile and run as the feature
lands.

### Fixed Array Basics

```python
values: array[i32][4] = [1, 2, 3, 4]
values[2] = values[0] + values[1]
```

### Matrix Math

```python
def mat4_mul(a: array[f32][4, 4], b: array[f32][4, 4]) -> array[f32][4, 4]:
    out: array[f32][4, 4]
    for row: i32 in range(4):
        for col: i32 in range(4):
            sum: f32 = 0.0
            for k: i32 in range(4):
                sum += a[row, k] * b[k, col]
            out[row, col] = sum
    return out
```

### Image Kernel

```python
def threshold(image: array[Color][160, 240], cutoff: f32):
    for y: i32 in range(160):
        for x: i32 in range(240):
            if image[y, x].luma() < cutoff:
                image[y, x] = Color.BLACK
```

### C Span Or Pointer Handoff

```python
pixels: array[Color][240 * 160]
upload_texture(&pixels[0], len(pixels))
```

### Imported Math Library

```python
import cpp "glm/glm.hpp" as glm

a: glm.vec3
b: glm.vec3
d = glm.dot(a, b)
```

### Library Tensor Hooks

```python
a: gpu.Tensor[f32, 2] = gpu.zeros[f32](rows, cols)
b: gpu.Tensor[f32, 2] = gpu.zeros[f32](rows, cols)

tile = a[y:y + 16, x:x + 16]
c = gpu.matmul(a, b)
```

The tensor hook example does not require a real GPU backend in the core test
suite. A CPU fake or small library fixture is enough to prove the language
syntax, overload hooks, and view/copy behavior.

## Acceptance

- `array[T][N]` fixed arrays compile and run.
- `array[T][M, N]` fixed matrices compile and run.
- `a[i]` and `a[i, j]` work with type checking.
- Slices have explicit view/copy semantics.
- target examples exist as fixtures or runnable examples.
- `list[T]` remains the normal dynamic owning container.
- Raw heap arrays are not the default user-facing story.
