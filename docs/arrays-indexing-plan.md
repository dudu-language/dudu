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

Library tensor types may also carry shape metadata:

```python
batch: tensor[f32][dyn, 784]
weights: tensor[f32][784, Classes]
```

`dyn` means an unrelated runtime-known dimension. Symbolic dimensions such as
`M`, `K`, `N`, `B`, `T`, or `Classes` are user-chosen names that track equality
relationships across a type or generic function:

```python
def matmul[M, K, N](
    a: tensor[f32][M, K],
    b: tensor[f32][K, N],
) -> tensor[f32][M, N]:
    ...
```

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

Status: explicit `array[T][N]`, `array[T][M, N]`, and higher-rank
`array[T][shape]` compile and lower to nested `std::array` storage.
Explicit-shaped literal initializers are checked for rectangular shape and
element type. `array[T] = literal` infers fixed shapes from non-empty
rectangular literals, including nested matrix and volume literals.

## Construction

Fixed arrays can be initialized with literals:

```python
xs: array[i32][4] = [1, 2, 3, 4]
ys: array[i32] = [1, 2, 3, 4]
```

Matrices and tensors can use nested literals:

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
nested `std::array` representation. A matrix multiply fixture exercises
function parameters, return values, nested loops, and comma indexing together.
An image-kernel fixture exercises inferred fixed-array shape, reference
parameters, comma indexing, member calls, and indexed mutation together.
Partial indexing such as `mat[row]` now returns the remaining fixed-array row
type, while over-indexing is diagnosed in Dudu source.

Status: fixed-array slicing no longer uses rank-specific row, column, channel,
or slab branches. Scalar, slice, ellipsis, and new-axis items lower through one
shape/stride/offset view path when the index expression produces a view, and
tensor-library indexing is separately routed through ordinary operator hooks.

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

Target spellings:

```python
row: array_view[f32] = mat[row_index, :]
patch: array_view[f32] = image[y0:y1, x0:x1]
copy: list[f32] = list(row)
```

If a source container cannot produce a safe view, the compiler should require an
explicit copy:

```python
copy = list(items[a:b])
```

Architecture correction: the prototype compiler grew separate slice-lowering
paths for one-dimensional spans, two-dimensional patches, two-dimensional
columns, three-dimensional channels, and `strided_span2` reslicing. That is the
wrong architecture. Built-in fixed arrays must lower through one generic
shape/stride view model, not through a new compiler branch per rank or pattern.

Target status: fixed-array slicing should produce `array_view[T]`, a generic
runtime shape/stride view. The parser keeps generic `IndexExpr(base, args...)`
and structured slice, ellipsis, and new-axis item nodes. Sema recognizes
fixed-array shape metadata and infers `array_view[T]` for fixed-array indexes
containing `:`, `...`, or `None`. Codegen lowers those fixed-array view forms
through the same helper that builds slice specs and computes `shape`,
`strides`, and `offset`.

Examples that must share the same path:

```python
row = mat[row_index, :]
col = mat[:, col_index]
patch = mat[y0:y1, x0:x1]
rgb = image[y, x, 0:3]
channel = image[:, :, channel_index]
whole = image[:, :, :]
```

`span[T]` and `strided_span[T]` may remain as low-level one-dimensional
interop/helper types, but fixed-array slicing should not infer them directly.
The old `strided_span2[T]` rank-2 helper is removed; two-dimensional and
higher-rank views use `array_view[T]`.

## Advanced Indexing Syntax

Advanced indexing syntax is language surface. Advanced tensor semantics are
library behavior. The full dispatch model is defined in
[Indexing Dispatch Model](indexing-dispatch-model.md).

Useful syntax targets:

```python
rgb = image[y, x, 0:3]
channel = image[:, :, channel_index]
tile = image[y:y + 8, x:x + 8]
train_x = x_norm[train_row, :]
correct = logits[arange(batch), label]
last_vocab = logits[..., -1]
broadcast_bias = bias[None, :]
```

For fixed Dudu arrays, slice-containing indexes produce `array_view[T]`
through the generic shape/stride path. This includes trailing-dimension range
slices after scalar prefixes, such as `image[y, x, 0:3]`, generic non-type
extents, and member-backed fixed arrays. There must be no separate compiler
branch for `image[y, x, 0:3]`, channel slicing, matrix patches, or tensor
slabs.

For library tensor types, the same syntax routes through `@operator("[]")`
and `@operator("[]=")` hooks. A tensor library can choose PyTorch-like
pairwise gather, NumPy-like behavior, cartesian helper members, lazy views, or
backend-specific results. The compiler only passes the structured index items.

## Tensor Policy Boundary

The array/indexing feature is general language surface, not a built-in tensor
framework. Core Dudu should understand fixed arrays, slices, comma indexing,
index assignment, shape metadata, and hook dispatch. It should not hard-code
NumPy, PyTorch, BLAS, GPU, autograd, or tensor-library semantics.

Advanced tensor behavior should be expressible by libraries through ordinary
`[]` hooks on tensor or indexer types:

```python
pairwise = logits[rows, cols]       # library-defined meaning
cartesian = logits.cartesian[rows, cols]
tiles = image.window[8, 8][y, x]
```

`cartesian`, `window`, `vindex`, `oindex`, or similar names are library API,
not compiler operators. They are ordinary members whose returned types can
implement normal `@operator("[]")` and `@operator("[]=")`. The compiler must
not contain special `vindex[]`, `oindex[]`, tensor, BLAS, or backend operator
names.

Conversion between library tensors and `array[T][shape]` should be explicit
unless the library can prove the value is already CPU-contiguous and safely
borrowable. GPU tensors, strided views, lazy expressions, and autograd-tracked
values need explicit copy/view/move APIs so allocation, device transfer, and
lifetime are visible.

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

Status: same-width Dudu-native `xyzw`, `rgba`, and `stpq` read swizzles are
implemented for local class receivers with matching component fields. Repeated
read components such as `v.xx`, `color.rrrr`, and `coord.qqqq` are allowed.
Same-width write swizzles such as `v.yx = other` and `color.bgra = other`
write component-by-component and reject repeated write components such as
`v.xx = other`. Different-width local Dudu-native read swizzles construct a
matching result class when one exists, such as `Vec4.xy -> Vec2`. Imported
vector swizzles use scanned field metadata for read and assignment, including
different-width results when a matching imported vector class exists. General
column/multidimensional slices and richer tensor-library metadata remain
compiler-architecture work.
Same-width read swizzles also work on expression receivers such as
`make_color().bgra`; emission uses a single-evaluation temporary for non-local
receivers.

## Library-Defined Indexing

The language should define indexing and slicing syntax, but libraries should be
able to own the semantics for their types.

The concrete numeric-stack follow-up is tracked in
[Tensor Backend And Numeric Stack Plan](tensor-backend-plan.md). That plan
keeps BLAS/GPU/autograd work in libraries and optional probes rather than
special-casing numeric backends in the compiler.

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
hooks. Simple containers can use fixed-arity hooks:

```python
class Grid:
    @operator("[]")
    def at(self, row: i32, col: i32) -> Cell:
        ...

    @operator("[]=")
    def set_at(self, row: i32, col: i32, value: Cell):
        ...
```

Tensor libraries need variadic typed hooks so one implementation can receive
arbitrary-rank index lists without rank-specific overloads:

```python
class Tensor[T]:
    @operator("[]")
    def at[Idx...](self, *idx: Idx) -> TensorSelection[T, Idx...]:
        ...

    @operator("[]=")
    def set_at[Idx...](self, *idx: Idx, value: TensorAssignable[T, Idx...]):
        ...
```

Each item keeps its real type: integer, `slice`, `ellipsis`, `new_axis`,
tensor/list/mask, or another library-defined index value. Tensor libraries can
map Dudu syntax to CPU views, GPU buffer views, lazy expressions, kernel
launches, or backend calls without the core language embedding
CUDA/Vulkan/OpenCL logic.

Status: Dudu-native `@operator("[]")` methods are recognized by indexing
semantics for read expressions, including index argument type checking. Indexed
member paths such as `self.values[i]` type-check through the same indexing
semantics. Dudu-native `@operator("[]=")` hooks type-check and lower indexed
assignments through the declared method for both local receivers such as
`tensor[i] = value` and member receivers such as `box.tensor[i] = value`.

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

Status: fixed arrays support `len(values)` and address-of indexed elements such
as `&values[0]` for imported C/C++ pointer-and-count APIs.

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
syntax, variadic overload hooks, and view/copy behavior. The fixture must not
depend on compiler knowledge of tensor/library names or rank-specific lowering.

## Acceptance

- `array[T][N]` fixed arrays compile and run.
- `array[T][M, N]` fixed matrices compile and run.
- `a[i]` and `a[i, j]` work with type checking.
- Slices have explicit view/copy semantics.
- library indexing supports structured slice, ellipsis, new-axis, and
  expression index items through normal operator hooks.
- target examples exist as fixtures or runnable examples.
- `list[T]` remains the normal dynamic owning container.
- Raw heap arrays are not the default user-facing story.
