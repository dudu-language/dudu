# Tensor Backend And Numeric Stack Plan

Dudu has enough fixed-array and basic indexing surface to start proving real
numeric workloads, but the tensor indexing model must stay Python-shaped and
library-owned. The next goal is to make NumPy/PyTorch-style indexing useful
with actual CPU and GPU-backed tensor libraries without baking BLAS, OpenCL,
ROCm, CUDA, PyTorch, or rank-specific tensor behavior into the compiler.

## Goal

Build a small Dudu numeric stack that demonstrates:

- fixed arrays and slices for small stack/local numeric work
- library-owned tensor values for dynamic CPU arrays
- BLAS-backed matrix/vector ops through normal C imports
- GPU-backed tensor storage through a portable backend on this machine
- Python-shaped indexing and slicing syntax lowering through ordinary Dudu
  hooks
- an autograd-style graph written in Dudu
- a small neural-network example, ideally MNIST-scale, that can run as an
  optional dogfood/probe

This is a dogfood and language-validation slice. It should not turn the Dudu
compiler into a tensor framework.

Required language/compiler fixtures live in this repository. They are the
source of truth for parser, sema, codegen, diagnostics, and LSP behavior.
Heavy backend checks such as BLAS, OpenCL, ROCm, CUDA, LibTorch, or dataset
training can be optional probes, but they should still be tracked as Dudu
validation work rather than hidden in unrelated scratch repos.

Dogfood repos such as `/home/vega/Coding/ML/dudu-datascience` are useful real
usage pressure and can contain aspirational API sketches, but they are not the
compiler suite. Once a dogfood sketch clarifies a language requirement, move
the required behavior into Dudu fixtures or optional probes.

A small passing BLAS demo is only a proof of native interop; it is not
completion of first-class tensor indexing.

The compiler/language indexing contract is defined in
[Indexing Dispatch Model](indexing-dispatch-model.md). This tensor plan must
follow that model. In particular, `tensor[rows, cols]`, `tensor[:, :, 0]`,
`tensor[..., -1]`, and `tensor[None, :]` are ordinary language syntax routed to
library hooks. `vindex`, `oindex`, `window`, or similar names may be ordinary
library members, but they are not Dudu syntax and must not be compiler
special-cased.

Autograd target ergonomics should feel closer to PyTorch than TensorFlow:
ordinary imperative model code, parameters used directly in tensor operations,
callable modules such as `model(x)`, `loss.backward()`, `opt.step()`, and
`opt.zero_grad()`. Tape/graph machinery may exist inside the library, but the
target API should not require user code to instantiate a public `Tape`.

## Non-Goals

- Do not special-case OpenBLAS, OpenCL, ROCm, CUDA, Eigen, PyTorch, or any
  library in semantic analysis or codegen.
- Do not add hidden copies for slices.
- Do not make GPU support mandatory for normal Dudu builds or tests.
- Do not build a full PyTorch clone in the compiler repo.
- Do not make the always-on fast test loop download datasets or compile heavy
  native libraries.

## Fixture Strategy

Add target fixtures before compiler/library implementation. The fixture suite
should make the intended language shape obvious and prevent another rank-2
shortcut from looking complete.

Required fixture groups:

- parser/AST fixtures for `x[:, ids, None, ...]`, nested index expressions,
  index assignment, and source ranges for each item
- fixed-array fixtures for scalar indexing, partial indexing, rank-independent
  slicing, `array_view[T]`, and shape inference
- library-hook fixtures for fixed-arity `[]`, fixed-arity `[]=`, variadic
  typed `[]`, variadic typed `[]=`, and overload diagnostics
- tensor API fixtures showing NumPy/PyTorch-like direct indexing:
  `tensor[rows, cols]`, `tensor[mask, :]`, `tensor[..., -1]`,
  `tensor[None, :]`, and `tensor[:, :, 0]`
- negative fixtures for unsupported item types, too many fixed-array indices,
  missing hooks, multiple ellipses, unsupported `ellipsis`, unsupported
  `new_axis`, bad scatter value types, and bad shape narrowing
- optional native probes for BLAS, OpenCL, ROCm/CUDA/LibTorch, and dataset
  examples when the local machine/tooling supports them

Before a feature compiles, target fixtures can live in an excluded
`target`/`spec` fixture folder or be represented as expected-failure cases if
the harness supports that cleanly. They should not be silently skipped without
being listed in this plan.

Current target fixtures live in `tests/targets/tensor_indexing`. The manifest
there records which examples already pass and which are intentional expected
failures. Run `scripts/check_targets.sh` to verify that no target silently
regressed and no expected-failure accidentally became a passing example without
being promoted into the normal fixture suite. The checker uses `--emit-cpp`
for normal pass/xfail cases, and `run` manifest entries compile and execute the
generated C++ for behavior that must not be fakeable by successful emission.

Status: `ndad_runtime_checks.dd` is a fast runtime fixture for the reference
tensor surface. It checks row-major strides, slice offsets, ellipsis, new-axis
insertion, direct advanced indexing, cartesian helper indexing, scalar point
writes, scalar slice fill, tensor-valued slice assignment, and boolean-mask
construction. This is intentionally inside the Dudu repo so a fake rank-2 or
ignored-index implementation cannot claim completion. The runtime target now
also checks mask-position gather and masked scalar scatter, including a
row-mask plus slice case, so masks must carry selected element positions rather
than only a selected count.

Status: `ndad` construction now uses one variadic `zeros[T, Dims...](*dims:
Dims)` path rather than rank-specific overloads, and `ndad_runtime_checks.dd`
constructs a rank-5 tensor to prove arbitrary-rank shape construction.
Imported Dudu functions preserve variadic pack metadata through module aliases,
so a library can define the pack once and consumers can import it normally.
`ndad_arbitrary_rank_runtime.dd` additionally compiles and runs rank-4 and
rank-5 direct indexing through the normal `ndad` operator hooks. It checks
mixed scalar/slice views, all-scalar `.item()` extraction, view
materialization, and mixed advanced indexing without adding tensor-name or
rank-specific compiler policy.

Status: `ndad_broadcasting_runtime.dd` proves broadcasting is library-owned
behavior behind ordinary Dudu operators. The fixture runs
`(x - mean[None, :]) * inv_std[None, :]` and a row-bias add using normal
`@operator("-")`, `@operator("*")`, and `@operator("+")` methods on
`Tensor[T]`. Shape selection and element loops live in `ndad_native.hpp`, not
in compiler tensor-name branches.

Status: `ndad_shape_preserving_helpers.dd` proves library code can preserve
same-shape tensor metadata through a variadic shape pack, compile the generated
C++, and run it. The matching negative target proves mismatched shape metadata
is diagnosed in Dudu source. Shape-only packs such as `Dims...` participate in
Dudu sema but are erased from generated C++ template argument lists unless the
parameter is required by emitted C++ types. Direct tensor operators can now
bind method generic packs from receiver shape metadata, so `a + b` can return
`Tensor[T][Dims...]` from `self: &Tensor[T][Dims...]` without a helper-only API.

Status: `ndad_broadcast_compatibility_runtime.dd` proves broadcasting
compatibility is checked by the tensor library. `can_broadcast(matrix, row)`
infers through shaped metadata on imported Dudu functions, while
`can_broadcast(matrix, bad)` rejects incompatible right-aligned dimensions.
Imported/native generic matching now peels shaped metadata when the expected
parameter is an unshaped generic library type, so helper APIs such as
`Tensor[T]` can accept `Tensor[i32][2, 3]` without explicit type arguments.

Status: `ndad_assignment_broadcast_runtime.dd` proves tensor-valued `[]=`
assignment uses the same broadcast-compatible shape model as normal tensor
operators. The reference library no longer cycles source elements with modulo;
it maps source offsets through right-aligned broadcast coordinates and rejects
incompatible assignment shapes in the library layer. The fixture covers row,
column, face, and singleton-axis assignment into rank-2 and rank-3 selections.
`ndad_repeated_scatter_runtime.dd` documents the current repeated advanced
scatter policy with executable evidence: destination offsets are visited in
index-result order, so repeated destinations are overwritten by the later
source element. This remains ordinary `ndad` library behavior, not a compiler
rule.

Status: `library_index_category_hooks.dd` proves `basic_index` is general
index-dispatch machinery, not a tensor special case. A simple library type can
route scalar/slice/ellipsis/new-axis packs to one overload and advanced
library-defined index objects to a generic variadic overload.

Status: `ndad_view_copy_runtime.dd` proves the in-repo ndad surface now makes a
real view/copy boundary observable at runtime. Basic direct indexing such as
`x[1, :]` returns a reference-backed `TensorView[T]` that can mutate the base
through `.fill(...)`; explicit `.to_tensor()` materializes an owning copy; and
advanced indexing such as `x[ids, :]` materializes a `Tensor[T]` that does not
alias the base. This remains ordinary library overload behavior: the compiler
only knows `basic_index` as a general scalar/slice/ellipsis/new-axis category
and does not know tensor names.

Status: `ndad_reduction_shape_runtime.dd` proves rank-generic tensor and view
helpers on the in-repo `ndad` surface. `Tensor[T]` and `TensorView[T]` expose
`.count()`, `.sum()`, `.mean()`, `.flatten()`, and `.reshape(...)` through
shape/stride/offset metadata and normal materialization helpers. The fixture
checks rank-3 tensors and non-contiguous views so reductions and reshape do
not depend on rank-2 `rows`/`cols` storage.

## Backend Choice

Use the most portable useful stack first:

- CPU BLAS: OpenBLAS / CBLAS. It is already in the optional compatibility
  matrix and is the right first acceleration proof.
- GPU: OpenCL first on this machine. It works on AMD, NVIDIA, and Intel more
  often than CUDA, and there is already an OpenCL optional probe.
- ROCm / rocBLAS: good AMD-specific follow-up if installed and usable.
- CUDA / cuBLAS: optional later probe when NVIDIA tooling and hardware exist.
- PyTorch C++ / LibTorch: valuable as a separate interop stress test, but not
  the first backend because distribution and ABI friction can dominate the
  language work.

## Library Shape

Create a Dudu library module or dogfood repo, not compiler built-ins:

```python
from c import cblas.h

class Tensor[T]:
    shape: list[i64]
    strides: list[i64]
    offset: i64
    data: list[T]

    @operator("[]")
    def view_at(self, *idx: basic_index) -> TensorView[T]:
        ...

    @operator("[]")
    def materialize_at[Idx...](self, *idx: Idx) -> Tensor[T]:
        ...

    @operator("[]=")
    def set_at[Idx...](self, *idx: Idx, value: TensorAssignable[T, Idx...]):
        ...
```

The exact API can evolve, but the important rule is that
`tensor[row, col]`, `tensor[y0:y1, x0:x1]`, `tensor[mask, :]`, and
`tensor[..., -1]` are normal Dudu operator calls on a library-defined type.
The core representation is rank-independent: `shape`, `strides`, and `offset`.
`rows` and `cols` are allowed convenience methods on rank-2 values, not the
foundation.

Status: the in-repo `tests/targets/tensor_indexing/ndad.dd` reference surface
now stores rank-independent `shape`, `strides`, and `offset` on `Tensor[T]` and
`TensorView[T]`. Arbitrary-rank index normalization is implemented as ordinary
library/native-boundary code in `ndad_native.hpp`; the compiler does not know
the tensor names and only dispatches the normal `@operator("[]")` /
`@operator("[]=")` hooks. Construction and broadcasting are also library code:
`zeros[T, Dims...]` forwards arbitrary-rank dimensions through a variadic pack,
and elementwise tensor operators use the same runtime shape/stride metadata for
right-aligned broadcasting.

Basic direct indexing now routes through `*idx: basic_index` overloads and
returns `TensorView[T]`; advanced direct indexing routes through generic
`Idx...` overloads and returns materialized `Tensor[T]`. One-element tensor or
view results expose PyTorch-style `.item()`; `ndad_item_runtime.dd` covers
scalar-looking basic selections, one-element slices, and one-element advanced
gathers. Returning a scalar directly when scalar indexes consume every tensor
axis still needs a rank/pack-count constraint feature; until that exists,
scalar extraction stays explicit rather than compiler policy.

## External Baseline

Dudu should feel familiar to people coming from Python numeric code, but it
should not copy the parts that numeric Python users already find confusing.

Useful baseline rules:

- NumPy and PyTorch make normal slicing view-like, while advanced indexing is
  not a simple view. Dudu should keep that distinction explicit in types and
  hovers.
- NumPy and PyTorch both accept rich Python indexing directly in brackets.
  Dudu should provide that syntax and let each tensor library choose the
  policy for mixed advanced indexing.
- If a library wants more than one policy, such as pairwise gather and
  cartesian gather, it can expose ordinary members like `.pairwise[...]`,
  `.cartesian[...]`, `.vindex[...]`, or `.oindex[...]`. Those names are not
  language features.
- Julia's array model is a useful reminder that indexing, views, broadcasting,
  and array traits can be library-extensible. Dudu should make tensor indexing
  a library capability, not a compiler patch for one tensor library.
- Broadcasting should be library/type-driven. The language should provide the
  syntax and operator hooks; the tensor library chooses CPU, BLAS, GPU, lazy
  expression, or materialized output.

References:

- NumPy indexing:
  <https://numpy.org/doc/stable/user/basics.indexing.html>
- NumPy NEP 21 advanced indexing:
  <https://numpy.org/neps/nep-0021-advanced-indexing.html>
- PyTorch tensor views:
  <https://docs.pytorch.org/docs/stable/tensor_view.html>
- Julia arrays:
  <https://docs.julialang.org/en/v1/manual/arrays/>

## Shape Annotations And Inference

Do not require users to put every dimension on the left side. If the right side
fully determines the shape, Dudu should infer it and let the LSP show the
result through inlay hints and hover.

```python
x = tensor.zeros[f32](32, 784)          # tensor[f32][32, 784]
w = tensor.zeros[f32](784, 256)         # tensor[f32][784, 256]
h = matmul(x, w)                        # tensor[f32][32, 256]
red = image[:, :, 0]                    # view or tensor expr with rank 2
```

Explicit shape annotations are still important at API boundaries and when the
author wants a shape assertion:

```python
def classify(x: tensor[f32][32, 784]) -> tensor[f32][32, 10]:
    return model(x)

logits: tensor[f32][32, 10] = classify(batch)
```

Runtime-known dimensions should be spelled `dyn`, not `?`.

```python
train_x = x_norm[train_row, :]          # tensor[f32][dyn, 784]
train_x: tensor[f32][dyn, 784] = x_norm[train_row, :]
```

`dyn` is a shape fact, not a mystery type. It means the dimension is known only
at runtime, usually because a mask or data-dependent gather selected an unknown
number of elements.

Each `dyn` is unrelated to every other `dyn`. Use symbolic dimensions when the
relationship matters:

```python
def matmul[M, K, N](
    a: tensor[f32][M, K],
    b: tensor[f32][K, N],
) -> tensor[f32][M, N]:
    ...

def classify[B, C](
    x: tensor[f32][B, 784],
    w: tensor[f32][784, C],
) -> tensor[f32][B, C]:
    ...
```

`M`, `K`, `N`, `B`, and `C` are user-chosen symbolic dimensions. They can be
compile-time-known or runtime-known; the type checker only needs to know that
the same symbol means the same dimension within the generic/type context.
Math-heavy code can use letters. Domain-heavy code can use names if they are
clearer, such as `Batch` or `Classes`.

The reference numeric stack names are:

- `ndad`: ndarray/tensor storage, indexing, BLAS, and GPU backends
- `mald`: ML/autograd/modules/optimizers built on top of `ndad`
- `ddtorch`: optional PyTorch-shaped frontend name if that split proves useful

## Copy Vs View Rules

Normal slice syntax should be view-like when the library can represent the
selection without allocating:

```python
row = tensor[3, :]        # view-like row selection
patch = image[y0:y1, x0:x1, :]  # view-like patch/channel selection
```

The compiler does not add hidden tensor copies for slices. A Dudu library may
return a `TensorView`, `Span`, lazy expression, or backend-specific view object
from `@operator("[]")`; the return type is the contract users see in hover and
inlay hints. If the user wants an owning result, the library should make that
explicit with an ordinary call such as `view.to_tensor()` or `tensor.copy(view)`.

Advanced indexing is not assumed to be view-like:

```python
pairwise = logits[rows, cols]          # library-defined gather policy
grid = logits.cartesian[rows, cols]    # explicit helper if the library wants one
masked = values[mask, :]               # runtime-sized selection
```

Those forms must be implemented by library-owned hooks with visible return
types. Repeated-index scatter, mask scatter, and accumulation order are library
policy. The compiler should route the syntax to hooks and reject unsupported
forms precisely; it should not silently invent NumPy/PyTorch copy behavior.

## Language Boundary

Dudu's job is to provide enough syntax and static typing to express serious
numeric APIs. Dudu's compiler should not become the numeric stack.

The compiler owns these general facilities:

- comma-separated indexing syntax
- `slice` values for `start:end:step` forms
- assignment through `@operator("[]=")`
- overload resolution for normal index hooks
- shape metadata on generic types, including `dyn`
- diagnostics when a type does not implement a requested index operation
- zero-copy-safe conversions only when the type contract proves compatible

Libraries own these policies:

- tensor storage layout
- CPU versus GPU residency
- BLAS, OpenCL, ROCm, CUDA, Vulkan, or other backend dispatch
- whether an index result is a view, lazy expression, or owning result
- broadcasting rules
- repeated-index scatter and accumulation policy
- autograd graph construction and tape ownership
- conversion/copy APIs between device tensors, CPU tensors, arrays, and views

This means `array[T][shape]` is a systems-language contiguous storage/view
facility, not Dudu's built-in NumPy. A tensor library may expose an explicit
CPU-contiguous view or copy when the storage makes that honest:

```python
cpu_view = tensor.as_array_view()   # zero-copy only if already CPU contiguous
cpu_copy = tensor.to_array()        # explicit materialization/copy
gpu_tensor = tensor.to(opencl.default()) # explicit device move/copy
```

Implicit passing to `array[T][...]`-like APIs is only acceptable when the
library type proves CPU-contiguous compatibility. GPU tensors, non-contiguous
views, remote buffers, lazy expressions, and autograd-tracked tensors must not
silently masquerade as arrays. They should require explicit copy/view/move APIs
so lifetime and allocation are visible.

Ownership belongs in library types using normal Dudu/C++ RAII rules. Stack
fixed arrays are inline values. Dynamic CPU tensors own heap buffers. Views
borrow storage and must have lifetimes expressible through normal Dudu
reference rules. GPU tensors own device resources and release them through
their destructor. The compiler should improve diagnostics around illegal view
escape, incompatible storage conversion, and `dyn` to concrete shape
assertions; it should not hard-code tensor allocation or freeing policy.

## De-Opinionated Indexing Model

The preferred design is direct Python-style indexing routed to ordinary
library hooks:

```python
pairwise = logits[rows, cols]
masked = values[mask, :]
last = logits[..., -1]
tiles = image.window[8, 8][y, x]
values = sparse.coo[rows, cols]
```

The compiler parses all of these as member/call/index expressions and resolves
the bracket operation through normal `@operator("[]")` and `@operator("[]=")`
overload dispatch. A library can expose helper members such as `window`, `coo`,
`cartesian`, `pairwise`, `vindex`, or `oindex`; those are normal members whose
returned objects implement normal indexing hooks.

Compiler-owned `@operator("vindex[]")`, `@operator("oindex[]")`, or other
named advanced-index operators are invalid architecture. They solve one
tensor-family case but make the language too opinionated.

The migration target is:

- `tensor[rows, cols]` routes to a tensor library's normal `[]` hook.
- `tensor.cartesian[rows, cols]` parses as member access followed by normal
  index if the library provides that helper.
- sema resolves each member and index step like any other expression.
- `[]=` and compound assignment use the same normal read/write hook rules.
- diagnostics mention missing members or missing `[]` hooks, not missing
  special compiler operators.
- existing dogfood examples are rewritten to direct tensor indexing unless the
  example is specifically proving a helper indexer object.

The compiler must not globally decide whether `tensor[rows, cols]` means
PyTorch-style pairwise gather, NumPy-style mixed advanced indexing, or
cartesian selection. The selected library hook decides through its return type
and implementation.

## Fancy Indexing Target Forms

The scratch dogfood file
`/home/vega/Coding/Web/dudu-webserver/indexingcoolness.dd` sketches the kind of
indexing Dudu should eventually make pleasant. That file is not the spec. The
compiler plan should use it as a source of target cases, then normalize those
cases into syntax that is close to Python/NumPy/PyTorch while avoiding their
known ambiguous corners.

Treat these as staged language/library targets. Early phases should reject
unsupported forms with precise diagnostics instead of silently lowering them
wrong.

### Already-Planned / Near-Term Fixed-Array Forms

These are the baseline syntax targets that should keep compiling as compiler
work continues:

```python
heatmap: array[i32] = [
    [1, 2, 3, 4],
    [10, 20, 30, 40],
    [100, 200, 300, 400],
]

center: i32 = heatmap[1, 2]
heatmap[2, 3] = heatmap[2, 3] + center

middle_row: array_view[i32] = heatmap[1, :]
second_column: array_view[i32] = heatmap[:, 1]
bright_patch: array_view[i32] = heatmap[1:3, 2:4]
```

Rank-3 image/tensor indexing should also stay concrete:

```python
pixels: array[i32] = [
    [[1, 2, 3, 255], [10, 20, 30, 255]],
    [[4, 5, 6, 255], [40, 50, 60, 255]],
]

green: i32 = pixels[1, 1, 1]
first_rgb: array_view[i32] = pixels[0, 0, 0:3]
all_green: array_view[i32] = pixels[:, :, 1]
all_values: array_view[i32] = pixels[:, :, :]
```

### Integer Gather

Integer index arrays can select coordinates. The library decides whether the
plain form is pairwise, NumPy-style mixed advanced indexing, or rejected in
favor of an explicit helper.

```python
label: tensor[i32][32]
logits: tensor[f32][32, 10]

correct_class_logit = logits[arange[32](), label]  # tensor[f32][32]
```

Transformer-style token embedding lookup is the same idea at higher rank:

```python
token_ids: tensor[i32][4, 16]
token_embedding: tensor[f32][32000, 512]

x = token_embedding[token_ids, :]  # tensor[f32][4, 16, 512]
```

Rule: gather is not assumed to be a view. It creates a gather expression or
owning result unless the backend can represent a lazy gather view.

### Orthogonal / Cartesian Gather

Sometimes each index array should create its own output axis instead of zipping
with the others. A library that needs this distinction can expose an explicit
helper member:

```python
batch_ids: array[i32] = [3, 0, 3, 1]
head_ids: array[i32] = [0, 2, 4]
hot_vocab: array[i32] = [2, 3, 5, 8, 13, 21]

cartesian = logits.cartesian[batch_ids, :, head_ids, hot_vocab]
# tensor[f32][4, 8, 3, 6]
```

The language requirement is that helper members work through normal member
lookup plus normal indexing hooks. The compiler does not own the cartesian
policy.

### Boolean Masks

Boolean masks should select rows/elements and produce dynamic-size axes when
the selected count is runtime-known:

```python
train_row: mask[bool][32]
x_norm: tensor[f32][32, 784]
label: tensor[i32][32]

train_x = x_norm[train_row, :]       # tensor[f32][dyn, 784]
train_label = label[train_row]       # tensor[i32][dyn]
```

Computed masks should work the same:

```python
visible: mask[bool][4, 16, 16] = frames[:, :, :, 3] > 0.0
visible_pixels = frames[visible, :]  # tensor[f32][dyn, 4]
```

Rule: `dyn` is a runtime-known dimension. It must be explicit in type displays
and diagnostics so users know shape is no longer compile-time fixed.

### Masked Assignment And Scatter

Mask assignment is scatter-style and should be visibly mutation:

```python
h1[dead_h1] = 0.0
h1[:, not active_h1_units] = 0.0
frames[not visible, :] = [1.0, 0.0, 1.0, 1.0]
logits[logits < -100.0] = 0.0
```

Integer gather assignment is also scatter-style:

```python
confusion[label[train_row], prediction[train_row]] += 1
logits[batch_ids, token_ids, :, hot_vocab] = -999.0
```

Scatter with repeated indices needs an explicit policy. For `+=`, backends may
need atomic/add-reduce behavior. The first implementation should either define
that policy clearly or reject ambiguous repeated-index scatter.

### Ellipsis, Negative Indices, And New Axes

These are valuable for ML-style code but should be staged after normal slices
and gather are solid:

```python
last_vocab_per_head: tensor[f32][4, 8, 6] = logits[..., -1]

per_head_bias: tensor[f32][6]
logits[:, :, :, :] += per_head_bias[None, None, :, None]
```

Rules:

- `...` preserves all unspecified middle axes.
- negative indices count from the end.
- `None` in indexing position inserts a size-1 broadcast axis and should not
  allocate.

### Broadcasting

Broadcasting should be library/type-driven, but the target syntax needs to be
clear:

```python
mean: tensor[f32][784]
inv_std: tensor[f32][784]
x: tensor[f32][32, 784]

x_norm = (x - mean[None, :]) * inv_std[None, :]
# tensor[f32][32, 784]

b1: tensor[f32][256]
h1 = matmul(x_norm, w1) + b1[None, :]
# tensor[f32][32, 256]
```

Broadcast errors must explain which dimensions do not match.

### Named-Axis Indexing

Named-axis indexing is a dream syntax, not an immediate compiler task, but it
captures the readability goal:

```python
rgb_tiles = frames[
    batch = :,
    height = windows.y,
    width = windows.x,
    channel = 0:3,
].copy()
```

This likely requires tensor types to carry axis-name metadata. It should remain
out of the first implementation unless the lower-level shape/indexing model is
already stable.

### Transformer And MLP Target Examples

The real target examples are not just small matrices. We should eventually have
non-default examples shaped like these:

```python
token_ids: tensor[i32][4, 16]
valid_token: mask[bool][4, 16]

x = token_embedding[token_ids, :]         # tensor[f32][4, 16, 512]
x += position_embedding[None, :, :]

qkv = einsum("btc,chd->bthd", x, wqkv)  # tensor[f32][4, 16, 3, 8, 64]
q = qkv[:, :, 0, :, :]                  # tensor[f32][4, 16, 8, 64]
k = qkv[:, :, 1, :, :]                  # tensor[f32][4, 16, 8, 64]
v = qkv[:, :, 2, :, :]                  # tensor[f32][4, 16, 8, 64]

scores[:, :, :, not valid_token] = -inf[f32]()
```

And a more grounded MLP/MNIST-style case:

```python
important_features: array[i32] = [0, 1, 2, 27, 28, 29, 391, 392, 393]
diagnostic_inputs = x_norm.cartesian[:, important_features] # tensor[f32][32, 9]

train_x = x_norm[train_row, :]                          # tensor[f32][dyn, 784]
correct_class_logit = logits[arange[32](), label]        # tensor[f32][32]
nll = -log_probs[train_row, train_label]                 # tensor[f32][dyn]
```

These examples should become tracked fixtures/examples as the syntax becomes
real. They do not belong in the fast default test suite.

## Implementation Phases

### 0. Pull Tensor Policy Out Of The Compiler

Before expanding the numeric stack, make the current indexing support less
opinionated:

1. Remove compiler-owned `vindex[]`, `vindex[]=`, `oindex[]`, and `oindex[]=`
   operator names.
2. Make direct `tensor[...]` advanced indexing and helper-member indexing
   resolve through ordinary member lookup plus ordinary `[]` / `[]=` hooks.
3. Add fixtures showing that custom indexers are not hard-coded:
   `tensor[...]`, `tensor.cartesian[...]`, `image.window[...]`,
   `sparse.coo[...]`, and a deliberately unsupported indexer that diagnoses
   a missing `[]` hook.
4. Audit semantic analysis and codegen for tensor-specific names. Any
   remaining special behavior should be moved to general member/index/call
   logic or deleted.
5. Keep only general compiler concepts: `slice`, index argument lists,
   shaped generic metadata, overload resolution, assignment hooks, and precise
   diagnostics.
6. Update the Dudu data-science dogfood repo and compiler fixtures to use
   indexer-object APIs.

Acceptance test: a new indexer spelling can be added entirely in Dudu library
code and used with `object.indexer[...]` without editing compiler sema or
codegen.

Status: complete for the compiler boundary. Existing fixtures prove
member indexer objects such as `tensor.pairwise[...]`, `tensor.cartesian[...]`,
chained temporary indexing such as `image.window[8, 9][y, x]`, and unrelated
custom `sparse.coo[...]` spellings can use normal library-owned `[]` / `[]=`
hooks. Python-style direct tensor indexing is the target path. `vindex` and
`oindex` remain only examples of names a user library could choose; they are
not compiler concepts or default dogfood API.

### 1. Audit Current Indexing Hooks

Verify the current compiler can express the library surface we need:

- `@operator("[]")` with multiple scalar indices
- `@operator("[]=")` with multiple scalar indices plus value
- method calls on indexed member receivers
- return types that are scalar, reference-like, view-like, or wrapper values
- slice arguments passed to library hooks, not only Dudu fixed arrays
- direct tensor gather forms such as `tensor[rows, cols]` routed to the
  library hook deliberately
- helper indexer forms such as `tensor.cartesian[...]` routed through normal
  member lookup and normal `[]` hooks
- mask indexing and masked assignment rejected or lowered deliberately
- good diagnostics when a tensor type does not implement a required hook

Add small compiler fixtures for each missing case before writing the tensor
library.

Status:

- Done: Dudu-native `@operator("[]")` read hooks and `@operator("[]=")` write
  hooks lower through named methods instead of pretending to be C++ `operator[]`.
- Done: multi-scalar indexing works for library receivers, including member
  receivers such as `box.tensor[1, 2]`.
- Done: indexed assignment hooks accept multiple scalar indices plus the
  assigned value.
- Done: slice expressions lower to a normal `slice` value when a
  library-defined indexing hook asks for one, so mixed forms such as
  `tensor[1:3, 2]` and `tensor[0:3:2, 4]` can dispatch through ordinary
  `@operator("[]")` / `@operator("[]=")` overloads.
- Done: Dudu code can inspect `slice.has_start`, `slice.has_end`,
  `slice.has_step`, `slice.start`, `slice.end`, and `slice.step`, so tensor
  libraries can compute view offsets and strides without compiler patches.
- Done: overloaded indexing hooks are selected by argument types, so one
  tensor type can expose scalar element access plus row, column, and patch
  slice views.
- Done: reference-backed view structs can be aggregate-initialized without
  invalid default reference fields.
- Done: one-element `Tensor[T]` and `TensorView[T]` values expose `.item()`
  for PyTorch-style scalar extraction without compiler rank shortcuts.
- Done: operator declarations now validate `[]` / `[]=` arity directly.
- Done: `.pairwise[...]`, `.cartesian[...]`, and other custom indexing spellings
  dispatch through ordinary member lookup to library-owned indexer objects
  whose types implement normal `@operator("[]")` and `@operator("[]=")`.
  The compiler no longer recognizes special `vindex[]` / `oindex[]` operator
  names.
- Done: direct Python-style advanced tensor indexing such as
  `tensor[rows, cols]`, `tensor[..., -1]`, `tensor[None, :]`, masks,
  scatter/assignment, and symbolic dimensions is covered by the current
  `tests/targets/tensor_indexing` manifest through ordinary fixed-arity and
  variadic `@operator("[]")` / `@operator("[]=")` hooks.
- Done: `ndad_direct_pairwise_runtime.dd` proves the policy split at runtime:
  direct `tensor[rows, cols]` is PyTorch-style pairwise gather/scatter, while
  `tensor.cartesian[rows, cols]` is an explicit helper object for orthogonal
  cartesian gather. The helper stores a pointer to the owning tensor storage so
  it observes later tensor mutation instead of carrying a stale copied list.
- Done: `basic_index` and `scalar_index` category types allow libraries to
  separate basic view-safe indexing from advanced materializing indexing without
  compiler tensor-name policy. `ndad_view_copy_runtime.dd` proves basic direct
  indexing can return a mutating `TensorView[T]`, while advanced indexing
  materializes a non-aliasing `Tensor[T]`.
- Done: Dudu class receivers without matching index hooks now diagnose missing
  `@operator("[]")` or `@operator("[]=")` directly instead of reporting
  "cannot index non-container".
- Covered by fixtures: `tests/fixtures/tensor_multi_index_hook.dd` and
  `tests/fixtures/tensor_slice_hook.dd` and
  `tests/fixtures/tensor_slice_views.dd` and `tests/fixtures/cpu_tensor_matmul.dd`
  and `tests/fixtures/tensor_pairwise_indexer_hook.dd` and
  `tests/fixtures/tensor_cartesian_indexer_hook.dd` and `tests/fixtures/bad_pairwise_indexer_missing_hook.dd` and
  `tests/fixtures/bad_cartesian_indexer_missing_hook.dd` and
  `tests/fixtures/custom_indexer_objects.dd` and
  `tests/fixtures/bad_tensor_missing_index_hook.dd` and
  `tests/fixtures/bad_tensor_missing_index_set_hook.dd` and
  `tests/fixtures/tensor_dogfood/shape_metadata.dd` and
  `tests/fixtures/tensor_dogfood/mask_indexing.dd`.
- Done: generic library types can carry a second shape-metadata bracket such as
  `Tensor[f32][dyn, 784]`. This metadata participates in Dudu type checking
  but lowers to the underlying library type, such as `Tensor<float>`, not to
  `std::array`. `dyn` means runtime-known dimension and is accepted as a
  wildcard in expected/API positions.
- Done: a library-defined `Mask` can participate in ordinary index hooks, so
  `tensor[mask, :]` can return a `Tensor[T][dyn, N]` value and
  `tensor[mask, :] = value` can lower to a normal `@operator("[]=")` scatter
  method without compiler knowledge of tensor backends.
- Done: a library-defined matrix mask can gather individual elements with
  `tensor[mask]` and scatter either a scalar fill or a same-length tensor value
  through overloaded `@operator("[]=")` hooks, keeping element-mask behavior in
  ordinary library code.
- Done: library hook return types preserve shape metadata through local RHS
  inference; `selected = tensor[mask, :]` can flow into an API requiring
  `Tensor[i32][dyn, 2]` without restating the local type.
- Done: scalar compound indexed assignment on library-owned receivers lowers
  through the same read/write hook pair as explicit indexing. For example,
  `tensor[i] += 1` calls `@operator("[]")` to read and `@operator("[]=")` to
  write the modified value.
- Done: compound scatter is rejected when the write hook does not accept the
  selected value type. For example, `tensor[mask, :] += 1` does not silently
  lower through a scalar-fill scatter hook; users must spell the read/modify
  value and `[]=` assignment explicitly so the library policy is visible.
- Done: compound assignment also works for member-owned advanced indexer
  objects when the library provides matching selected-value write hooks.
  `tensor.pairwise[...] += x` lowers to a normal `[]` read plus `[]=` write on
  the `pairwise` object, and `tensor.cartesian[...] += x` does the same for that
  library-owned helper object. New tensor work should prefer direct
  Python-style `tensor[...]` examples unless the point of the fixture is
  proving helper indexer objects.
  Repeated-index scatter order and accumulation behavior is therefore a
  library policy, not a compiler policy.
- Done: `assume_shape[T](value)` lets library/user code narrow a runtime-known
  `dyn` shaped value to a concrete shaped type after explicit runtime checks.
  The builtin is restricted to shaped target types and lowers to the value
  expression, so it carries only type metadata and does not add hidden tensor
  copies or backend behavior. `ndad_assume_shape_runtime.dd` proves the
  positive tensor path, while `bad_ndad_dyn_to_concrete_without_assume.dd` and
  `bad_ndad_assume_shape_base_type.dd` prove that concrete APIs reject raw
  `dyn` values and that `assume_shape` cannot change the tensor base type.
- Done: generic functions can use shape parameters in library tensor metadata,
  such as `Tensor[f32][Rows, Inner]`, and propagate composed result shapes like
  matrix multiply without leaking erased shape-only parameters into generated
  C++ templates. Conflicting shape inference is diagnosed in Dudu source,
  including after a shaped result flows into later same-shape tensor helpers.
- Done: imported generic Dudu helpers preserve both their generic type
  arguments and shaped metadata through RHS inference, so
  `values = zeros[f32](4, 2)` can feed `values[mask, :]` and surface
  `Tensor[f32][dyn, 2]` in editor type information.
- Done: composed shape facts propagate through runtime-sized mask selections
  and later generic tensor APIs. For example, `values[mask, :]` can flow into a
  generic matmul-style helper and produce `Tensor[i32][dyn, 3]` without a local
  left-side shape annotation.
- Done: codegen-side local type inference preserves explicit generic function
  return substitutions, so generic tensor locals inferred from calls such as
  `zeros[i32](...)` can still lower scalar `@operator("[]=")` writes through
  the write hook instead of falling back to read-accessor assignment.
- Done: reusable tensor views now own common non-allocating behavior such as
  fill, sum, mean, and max. Owning view materialization remains an explicit
  helper call because methods returning the owning tensor currently run into
  the compiler's C++ incomplete-type emission boundary.
- Remaining: out-of-line method emission if libraries need mutually referential
  value/view methods returning complete owning types, plus broader shape
  propagation only as future examples demand it.

### 2. CPU Tensor Library

Add a small Dudu CPU tensor library in examples or a dogfood repo:

- `Tensor[f32]` with row-major storage
- constructors: zeros, filled, from literal/list where practical
- scalar indexing and indexed assignment
- row, column, and patch views where the current slice model supports it
- elementwise add/sub/mul
- map/apply for activation functions
- matrix multiply via CBLAS when available
- pure Dudu fallback matmul for non-BLAS validation and tiny tests

The BLAS call should be a normal imported C function. If a small adapter is
needed for enum constants or CBLAS layout values, keep it as a user-library
adapter, not a compiler patch.

The library should also prove the storage/conversion boundary:

- stack/local fixed arrays remain `array[T][shape]`
- dynamic CPU tensors own heap storage through RAII
- row/column/patch selections return borrow/view types, not hidden copies
- explicit `.copy()`, `.to_array()`, or `.to_tensor()` materializes owning
  storage
- explicit `.as_array_view()` works only for CPU-contiguous storage and
  rejects/diagnoses GPU, non-contiguous, or lazy-expression values
- shape assertions such as `assume_shape` remain explicit and diagnostic-rich

Status:

- First compiler-resident dogfood fixture exists in
  `tests/fixtures/cpu_tensor_matmul.dd`.
- It defines a small Dudu `Tensor` class with row-major `list[f32]` storage,
  `filled`, `zeros`, scalar indexing, indexed assignment, elementwise add, and
  pure Dudu matmul.
- Slice hook arguments are now available to library code as `slice` values, so
  row, column, and patch view APIs can be implemented as ordinary Dudu methods
  instead of fixed-array-only compiler behavior.
- `tests/fixtures/tensor_slice_views.dd` and `examples/tensor_views.dd` define
  row, column, and patch views backed by the original tensor storage through a
  reference to the underlying `list[f32]`.
- `tests/fixtures/tensor_dogfood/tensor.dd` is a reusable Dudu tensor module
  with scalar indexing, indexed assignment, row/column/patch views, elementwise
  add/sub/mul, `scale`, transpose, reductions, mean squared error, mean
  absolute error, binary accuracy, callback-based `map_values`, `relu`,
  `leaky_relu`, `clamp`, `threshold`, row-mask selection/scatter, view
  copy/fill/sum helpers, row-bias add, and pure Dudu matmul.
- `tests/fixtures/tensor_dogfood/views_main.dd` validates the reusable module's
  matrix/image-style view behavior through normal imports, including view-owned
  fill/reduction helpers.
- `tests/fixtures/tensor_dogfood/mask_rows_main.dd` validates reusable row-mask
  read and explicit read/modify/write masked scatter. The separate generic
  `mask_indexing.dd` fixture validates `dyn` shaped flow.
- `tests/fixtures/tensor_dogfood/shape_mask_composition.dd` validates `dyn`
  mask-selection shape propagation through a later generic matmul-style helper
  and covers scalar `[]=` writes on a generic tensor local inferred from an
  explicit generic function call.
- `tests/fixtures/tensor_dogfood/mask_elements_main.dd` validates reusable
  element-mask gather, scalar masked fill, and tensor-valued masked scatter.
- `tests/fixtures/tensor_dogfood/activation_metrics_main.dd` validates broader
  activation and metric helpers through normal imported Dudu tensor code.
- `tests/fixtures/tensor_dogfood/xor_main.dd` validates a tiny generated XOR
  classification path using `matmul`, row bias, and callback activation.
- `tests/fixtures/tensor_dogfood/tiny_training_main.dd` validates a tiny
  one-layer OR classifier trained with batch gradient descent using pure Dudu
  tensor helpers, reductions, matmul, and indexed parameter updates.
- `tests/fixtures/tensor_dogfood/autograd_main.dd` validates the start of an
  autograd-style graph using normal Dudu enums, classes, lists, operators, and
  explicit reference mutation.
- `tests/fixtures/tensor_dogfood/openblas_compare.dd` compares reusable Dudu
  tensor `matmul` against CBLAS `sgemm` through normal C interop.
- `/home/vega/Coding/ML/dudu-datascience/src/blas_demos.dd` is a separate
  dogfood repo demo that links `openblas` through `dudu.toml`, imports
  `cblas.h`, compares pure Dudu row-major matmul against `cblas_sgemm`, and
  prints verifiable results.
- Done for proof only: the compiler repo and `dudu-datascience` prove CBLAS
  interop with tiny row-major tensors.
- Remaining for the actual target API: build a reusable `dudu_tensor` library
  that satisfies the target files in `dudu-datascience/spec/target_api`,
  including owning tensors, view types, explicit materialization, shape
  metadata, broadcasting, backend dispatch, and diagnostics.

### 3. Optional OpenBLAS Probe

Add a non-default optional probe:

```text
scripts/probe_optional.sh openblas_tensor_matmul
```

It should:

- build through normal `dudu build`
- link OpenBLAS via pkg-config/CMake config where available
- compare BLAS matmul result against the pure Dudu fallback on a small matrix
- fail clearly when OpenBLAS is not installed

This belongs in the compatibility matrix, not the always-on unit tests.

Status:

- Done: `tests/fixtures/openblas_sgemm.dd` validates CBLAS `sgemm` through
  normal C header interop, alongside the existing `openblas_ddot` probe.
- Done: `tests/fixtures/tensor_dogfood/openblas_compare.dd` validates reusable
  tensor `matmul` against CBLAS `sgemm`.
- Done: `dudu-datascience` includes a user-facing OpenBLAS matmul demo that
  exercises the same boundary outside the compiler repo.
- Done: `scripts/probe_optional.sh` runs ddot, sgemm, and reusable tensor
  compare probes when OpenBLAS is discoverable through `pkg-config`.
- Done: native enum/type compatibility now handles the qualified CBLAS enum
  spellings without OpenBLAS-specific compiler cases.
- Remaining: no OpenBLAS work for this slice; GPU probing is tracked below.

### 4. GPU Tensor Backend

Add an optional OpenCL-backed tensor example:

- allocate device buffers
- upload/download `Tensor[f32]`
- run a simple elementwise kernel
- run a simple matmul kernel or call a backend BLAS if available
- expose the same high-level Dudu API shape where possible

Keep the first GPU test small and deterministic. It should prove the language
can drive GPU buffers and kernels through imports and normal wrapper code. It
does not need to beat optimized BLAS immediately.

The GPU API should mirror CPU tensor shape and indexing where practical, but
storage movement must remain explicit:

```python
cpu = tensor.zeros[f32](32, 784)
gpu = cpu.to(opencl.default())
result = gpu.matmul(weights_gpu)
back = result.cpu()
```

Indexing a GPU tensor may return a GPU view, a lazy gather expression, or a
backend-specific value. It must not silently copy to CPU just because the user
used `[]`. CPU array compatibility should require an explicit download/copy.

Status:

- Done: `tests/fixtures/tensor_dogfood/opencl_tensor_add.dd` uses the reusable
  Dudu `Tensor` storage, creates OpenCL buffers through the C API, runs a tiny
  elementwise kernel, reads back into a Dudu tensor, and validates the result.
- Done: `tests/fixtures/tensor_dogfood/opencl_tensor_matmul.dd` uploads two
  reusable Dudu tensors, runs a tiny OpenCL matmul kernel, reads back into a
  Dudu tensor, and compares the result against pure Dudu `matmul`.
- Done: `scripts/probe_optional.sh opencl` runs the existing host API probe and
  the tensor add/matmul probes when OpenCL is discoverable through
  `pkg-config`.
- Remaining: backend BLAS probes such as rocBLAS/cuBLAS are optional follow-up
  work when local hardware and tooling make them practical.

### 5. Autograd Prototype

Implement a small autograd graph in Dudu using ordinary classes:

- `Value` or `TensorNode`
- operation enum / tagged node kind
- parents/children or input refs
- forward value
- gradient
- backward pass
- operations: add, mul, matmul, relu, mean squared error

User-facing training code should use callable modules and `loss.backward()`
rather than a public `Tape` object:

```python
pred = model(x)
loss = mse_loss(pred, y)
loss.backward()
opt.step()
opt.zero_grad()
```

A tape or graph can be an internal implementation detail of the tensor/autograd
library.

This primarily stresses:

- classes and references/pointers
- lists of graph nodes
- enums or sum types
- operator overloads
- tensor method calls
- destructor/RAII behavior when graph values go out of scope

### 6. Tiny Neural Network Example

Add an optional dogfood example:

- start with XOR or a tiny generated classification dataset
- then add MNIST as an optional dataset-driven example
- train a one-hidden-layer MLP
- report loss/accuracy
- keep CPU fallback always available
- use BLAS when available
- use GPU backend when available

MNIST should not be part of fast tests. If used, add a script that downloads or
locates the dataset and documents the dependency.

### 7. LSP And Docs

Make sure tensor code is pleasant to write:

- hover on tensor indexing shows the selected `@operator("[]")` signature
- definition on `tensor[row, col]` can jump to the operator implementation
- inlay hints show inferred tensor/view types
- diagnostics explain missing index hooks
- docs show copy-vs-view behavior clearly

Status:

- Done: definition and hover on a bracket/operator position in `tensor[...]`
  resolve to the selected Dudu `@operator("[]")` hook, including its signature
  and docs. Clicking normal index variables inside the brackets remains normal
  symbol navigation.
- Done: inferred tensor/view shaped types show up in LSP inlay hints and
  tooltip payloads, including imported generic tensor hooks such as
  `Tensor[f32][dyn, 2]`.

## Acceptance

This slice is done when:

- current compiler tests stay green
- a Dudu CPU tensor example compiles and runs
- an OpenBLAS-backed optional probe compiles, links, runs, and validates output
- an OpenCL GPU optional probe compiles and runs on this AMD machine if the
  system has usable OpenCL drivers
- indexing/slicing hooks used by the tensor library are covered by compiler
  fixtures
- no compiler code special-cases a specific numeric library
- docs and the native compatibility matrix list the new probes and their status

The broader first-class tensor-indexing goal is done only when the
`dudu-datascience/spec/target_api` examples either compile as written or have
been replaced by equivalent built examples with the same user-facing semantics.
Passing toy demos alone does not satisfy that goal.

## Suggested Overnight Goal

Execute this plan through the first useful vertical slice:

1. Audit and complete the missing language support for library tensor indexing
   hooks.
2. Build a CPU `Tensor[f32]` Dudu library with scalar indexing, assignment,
   pure Dudu matmul, and OpenBLAS-backed matmul when available.
3. Add optional OpenBLAS validation and update the compatibility matrix.
4. Start the OpenCL backend only after CPU/BLAS is green.

Do not spend the night fighting CUDA on an AMD machine. Use OpenCL first, and
only add ROCm/rocBLAS if the tooling is already present and easy to discover.
