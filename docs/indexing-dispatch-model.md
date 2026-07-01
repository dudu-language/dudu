# Indexing Dispatch Model

Dudu indexing should recreate the useful surface of Python numeric indexing
without baking NumPy, PyTorch, BLAS, CUDA, OpenCL, or any tensor library into
the compiler.

The important boundary is:

- the language owns bracket syntax and structured index arguments
- fixed Dudu arrays own predictable contiguous array/view semantics
- libraries own tensor, matrix, mask, gather, scatter, broadcast, backend, and
  autograd semantics

## Baseline From Existing Languages

The target behavior is grounded in how mature systems already work:

- NumPy parses `x[a, :, b, None, ...]` into structured index records before
  deciding scalar, view, boolean, or fancy-index behavior. See the local source
  snapshot at
  `/home/vega/Coding/LangDev/numpy/numpy/_core/src/multiarray/mapping.h` and
  `/home/vega/Coding/LangDev/numpy/numpy/_core/src/multiarray/mapping.c`.
- PyTorch exposes the same idea to C++ as a sequence of `TensorIndex` values.
  See `/home/vega/Coding/LangDev/pytorch/aten/src/ATen/TensorIndexing.h` and
  `/home/vega/Coding/LangDev/pytorch/aten/src/ATen/TensorIndexing.cpp`.
- Julia lowers indexing to `getindex(A, I...)` and normalizes index arguments
  through array/library dispatch. See
  `/home/vega/Coding/LangDev/julia/base/multidimensional.jl` and
  `/home/vega/Coding/LangDev/julia/base/views.jl`.

The shared lesson is that syntax is generic. Tensor policy is not syntax.

## Decisions

These are settled design points for the implementation work:

1. Indexing syntax should stay as close to Python as possible:
   `x[:, ids, None, ...]`, not marker functions or Dudu-only index syntax.
2. Runtime-known dimensions are spelled `dyn`, as in
   `Tensor[f32][dyn, 784]`. This is the Dudu-short spelling of C++ concepts
   such as Eigen's `Dynamic` or `std::dynamic_extent`.
3. Direct bracket indexing is the normal tensor surface. A NumPy/PyTorch-like
   library should make `tensor[rows, cols]` feel like Python numeric code.
4. `cartesian[...]`, `pairwise[...]`, `vindex[...]`, `oindex[...]`, `window[...]`,
   or similar names are ordinary library helper members only.
5. Complex indexing stays under the same `@operator("[]")` and
   `@operator("[]=")` operators as simple indexing. Simple containers can
   implement fixed-arity overloads; tensor libraries can implement variadic
   typed overloads.
6. Macros are not a prerequisite for this work. Variadic generics and
   structured index item AST/sema are the key language prerequisites.
7. Required language/compiler fixtures live in the Dudu repo. Heavy native
   backend probes such as BLAS/OpenCL/CUDA can be optional, but they still
   belong to the Dudu validation story. Dogfood repos are usage pressure, not
   the source of truth.
8. Cross-library tensor movement should be explicit and zero-copy only when
   storage, device, layout, and lifetime make it honest. A `mald` or
   `ddtorch` value should be able to hand a non-copying view/move to `ndad`
   when possible, and require an explicit copy/move otherwise.
9. The reference numeric stack names are `ndad` for ndarray/tensor storage,
   indexing, BLAS, and GPU backends, and `mald` for the ML/autograd/module
   layer built on top. `ddtorch` remains a viable PyTorch-shaped library name
   if a separate torch-like frontend is useful.
10. Symbolic dimensions are user-chosen names. `M`, `K`, `N`, `B`, `T`,
    `C`, `H`, `W`, or domain names such as `Classes` are not built-ins; the
    same symbol simply means the same dimension in that generic/type context.

## Syntax

The parser must keep indexing as a structured expression:

```text
IndexExpr(base, items...)
```

Each item is one of:

- ordinary expression: `i`, `row_ids`, `mask`, `x + 1`
- slice: `:`, `start:stop`, `start:stop:step`
- ellipsis: `...`
- new axis: `None` when used inside an index list

Examples:

```python
value = xs[i]
value = mat[row, col]
row = mat[row_index, :]
patch = image[y0:y1, x0:x1, :]
last = logits[..., -1]
expanded = bias[None, :, None]
gathered = logits[batch_ids, :, head_ids, vocab_ids]
```

`:` is not a standalone expression outside indexing. Inside indexing it creates
a `slice` value. `...` creates an `ellipsis` value. `None` creates a `new_axis`
value only in indexing position; normal `None` expression rules still apply
elsewhere.

The parser must not collapse comma indexing into an ordinary tuple literal.
`x[a, b]` is an index expression with two index items, not `x[(a, b)]`.

## Dispatch

Dudu has two indexing paths.

### Fixed Dudu Arrays

`array[T][shape]` is a core contiguous storage type. The compiler may provide
built-in scalar and slice behavior for fixed arrays because fixed arrays are a
language type.

Rules:

- all scalar indices produce an element or lower-rank fixed array reference
- any slice item produces an `array_view[T]`
- shape, stride, and offset computation is rank-independent
- no compiler path may be specific to rows/columns/channels/images

Valid examples:

```python
matrix: array[f32][4, 4]
value = matrix[y, x]
row = matrix[y, :]
patch = matrix[y0:y1, x0:x1]

image: array[u8][480, 640, 4]
rgb = image[y, x, 0:3]
red = image[:, :, 0]
```

The implementation must use one generic shape/stride/view builder for all
ranks. Separate compiler branches such as "row slice", "column slice",
"rank-3 channel slice", or "rank-4 tensor slab" are architecture bugs.

### Library Types

For every non-core container, Dudu dispatches indexing through ordinary
operator overloads.

Fixed-arity hooks remain valid for simple containers:

```python
class Grid:
    @operator("[]")
    def at(self, y: i32, x: i32) -> Cell:
        ...

    @operator("[]=")
    def set_at(self, y: i32, x: i32, value: Cell):
        ...
```

Full numeric/tensor libraries need variadic typed index hooks so one
implementation can accept arbitrary-rank index lists without rank-specific
overloads:

```python
class Tensor[T]:
    @operator("[]")
    def at[Idx...](self, *idx: Idx) -> TensorSelection[T, Idx...]:
        ...

    @operator("[]=")
    def set_at[Idx...](self, *idx: Idx, value: TensorAssignable[T, Idx...]):
        ...
```

This is the Dudu version of Julia's `getindex(A, I...)` and C++ parameter-pack
dispatch. Each index item keeps its real type:

- scalar integer stays an integer type
- `:` / `a:b:c` becomes `slice`
- `...` becomes `ellipsis`
- index `None` becomes `new_axis`
- tensor/list/mask index expressions keep their library-defined type

The compiler does not decide whether `tensor[rows, cols]` is pairwise gather,
cartesian gather, a view, a lazy expression, or an owning result. The selected
library operator decides that through its return type and implementation.

Implementation status: expression-level pack expansion such as `idx...` is now
parsed as structured AST and can be forwarded through ordinary generic calls.
The code generator emits variadic `[]=` methods with the assigned value before
the C++ parameter pack while preserving Dudu source syntax as
`def set_at[Idx...](self, *idx: Idx, value: T)`. This keeps C++ template
deduction valid without changing the Dudu surface.

Implementation status: imported Dudu functions and methods preserve variadic
metadata through the module-alias/native-signature boundary. A library module
can expose a single pack-based API such as `zeros[T, Dims...](*dims: Dims)`,
and callers importing that symbol still get true variable arity instead of a
fake one-parameter native signature.

## Python Numeric Target

The following forms are target Dudu syntax. They should be accepted by the
language and routed to library hooks without compiler tensor policy:

```python
train_x = x_norm[train_row, :]
train_label = label[train_row]

correct = logits[arange(batch), label]
token_vectors = embedding[token_ids, :]

heads = qkv[:, :, 0, :, :]
last_vocab = logits[..., -1]

x_norm = (x - mean[None, :]) * inv_std[None, :]
image_rgb = image[:, :, 0:3]
```

A library may intentionally choose PyTorch-like pairwise semantics for
`logits[rows, cols]`. Another library may reject ambiguous advanced indexing
and expose explicit helper members such as `tensor.cartesian[...]` or
`tensor.pairwise[...]`. Those are library APIs, not language syntax.

`vindex` and `oindex` are not Dudu language concepts. They may exist as
ordinary library-owned members if a library wants those names, but the compiler
must not contain special handling for them.

## Assignment And Scatter

Indexed assignment uses the same dispatch model:

```python
tensor[mask, :] = 0.0
tensor[rows, cols] += 1.0
```

`[]=` hooks receive the same structured index items plus the assigned value.
Repeated indices, atomic accumulation, scatter ordering, gradient behavior, and
backend synchronization are library policy. If a library cannot define a safe
meaning for a scatter form, it should reject the overload with a clear
diagnostic.

Compound assignment is defined as a read through `[]`, an operation, then a
write through `[]=`. If that is not safe for a backend, the library type should
avoid exposing the matching hook or require an explicit method.

## Diagnostics

Diagnostics should describe the missing operation, not a tensor guess:

- missing `@operator("[]")` for receiver type
- no overload accepting the item types `(slice, Tensor[i32], new_axis)`
- fixed array indexed with too many scalar dimensions
- fixed array slice cannot produce a safe view
- unsupported `ellipsis` or `new_axis` for a library type

Diagnostics must point at the relevant index item where possible.

## Implementation Requirements

The compiler work needed for this model is:

1. Preserve `IndexExpr(base, items...)` and item source ranges in the AST.
2. Represent slice, ellipsis, and new-axis index items as structured nodes or
   values, not raw text.
3. Add variadic typed parameters for Dudu functions/operators:
   `def f[T...](self, *items: T)`.
4. Resolve fixed-arity and variadic `@operator("[]")` / `@operator("[]=")`
   overloads from the structured item list.
5. Lower fixed-array slicing through one rank-independent shape/stride
   `array_view[T]` builder.
6. Lower library indexing to calls on the selected operator implementation
   without name-specific tensor branches.
7. Delete compiler-owned `vindex`, `oindex`, row/column/channel/slab special
   paths, and any rank-specific tensor view lowering.
8. Add fixtures that prove new indexer APIs can be written in Dudu library code
   without modifying sema or codegen. The current target manifest is
   `tests/targets/tensor_indexing/manifest.tsv`, checked by
   `scripts/check_targets.sh`.

Current status: fixed-array slice inference and emission use the generic
`array_view[T]` builder path. Prototype fixed-array branches for row views,
columns, full matrices, matrix patches, image channels, and trailing channel
ranges have been removed. Low-level explicit helper types such as
`strided_span[T]` may still define its own one-dimensional helper behavior, but
fixed arrays do not route through rank-specific branches. The old
`strided_span2[T]` helper is removed.

The tensor library work needed after that is:

1. Build rank-independent `Tensor[T]` and `TensorView[T]` storage around
   `shape`, `strides`, and `offset`.
2. Implement `@operator("[]")` / `@operator("[]=")` with variadic typed index
   hooks.
3. Define view/copy/lazy/scatter behavior in the library API and make it
   visible through return types and hover.
4. Add BLAS/OpenCL/other backend dispatch behind normal library calls.
5. Prove the target API with real examples, not rank-2 demos.

## Non-Negotiable Boundaries

- No compiler special cases for tensor library names.
- No compiler special cases for rank 2, image channels, batches, heads, or
  vocabulary axes.
- No hidden tensor copies inserted by the compiler.
- No syntax that exists only to support one backend.
- No "demo passed" claim when the demo depends on rank-specific compiler or
  library shortcuts.
