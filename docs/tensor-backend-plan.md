# Tensor Backend And Numeric Stack Plan

Dudu already has enough array/indexing surface to start proving real numeric
workloads. The next goal is to make the fancy indexing useful with actual CPU
and GPU-backed tensor libraries without baking BLAS, OpenCL, ROCm, CUDA, or
PyTorch special cases into the compiler.

## Goal

Build a small Dudu numeric stack that demonstrates:

- fixed arrays and slices for small stack/local numeric work
- library-owned tensor values for dynamic CPU arrays
- BLAS-backed matrix/vector ops through normal C imports
- GPU-backed tensor storage through a portable backend on this machine
- indexing and slicing syntax lowering through ordinary Dudu hooks
- an autograd-style graph written in Dudu
- a small neural-network example, ideally MNIST-scale, that can run as an
  optional dogfood/probe

This is a dogfood and language-validation slice. It should not turn the Dudu
compiler into a tensor framework.

## Non-Goals

- Do not special-case OpenBLAS, OpenCL, ROCm, CUDA, Eigen, PyTorch, or any
  library in semantic analysis or codegen.
- Do not add hidden copies for slices.
- Do not make GPU support mandatory for normal Dudu builds or tests.
- Do not build a full PyTorch clone in the compiler repo.
- Do not make the always-on fast test loop download datasets or compile heavy
  native libraries.

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
    rows: i32
    cols: i32
    data: list[T]

    @operator("[]")
    def index(self, row: i32, col: i32) -> T:
        return self.data[row * self.cols + col]

    @operator("[]=")
    def set_index(self, row: i32, col: i32, value: T):
        self.data[row * self.cols + col] = value
```

The exact API can evolve, but the important rule is that `tensor[row, col]`
and `tensor[y0:y1, x0:x1]` are just normal Dudu operator/language hooks on a
library-defined type.

## External Baseline

Dudu should feel familiar to people coming from Python numeric code, but it
should not copy the parts that numeric Python users already find confusing.

Useful baseline rules:

- NumPy and PyTorch make normal slicing view-like, while advanced indexing is
  not a simple view. Dudu should keep that distinction explicit in types and
  hovers.
- NumPy's mixed advanced indexing has a long-known ambiguity between pairwise
  and cartesian selection. Dudu should provide explicit `.vindex[...]` and
  `.oindex[...]` forms instead of inventing marker expressions inside `[]`.
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
    return model.forward(x)

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

middle_row: span[i32] = heatmap[1, :]
second_column: strided_span[i32] = heatmap[:, 1]
bright_patch: strided_span2[i32] = heatmap[1:3, 2:4]
```

Rank-3 image/tensor indexing should also stay concrete:

```python
pixels: array[i32] = [
    [[1, 2, 3, 255], [10, 20, 30, 255]],
    [[4, 5, 6, 255], [40, 50, 60, 255]],
]

green: i32 = pixels[1, 1, 1]
first_rgb: span[i32] = pixels[0, 0, 0:3]
all_green: strided_span[i32] = pixels[:, :, 1]
all_values: span[i32] = pixels[:, :, :]
```

### Pairwise Integer Gather

Integer index arrays with matching shape can select pairwise coordinates. This
is the important "get the selected class/logit for each row" form:

```python
label: tensor[i32][32]
logits: tensor[f32][32, 10]

correct_class_logit = logits.vindex[arange[32](), label]  # tensor[f32][32]
```

Transformer-style token embedding lookup is the same idea at higher rank:

```python
token_ids: tensor[i32][4, 16]
token_embedding: tensor[f32][32000, 512]

x = token_embedding.vindex[token_ids, :]  # tensor[f32][4, 16, 512]
```

Rule: pairwise gather is not a view. It creates a gather expression or owning
result unless the backend can represent a lazy gather view.

Plain `tensor[ids, labels]` may eventually be accepted for the common pairwise
case, but mixed advanced indexing should initially diagnose and suggest
`.vindex[...]` or `.oindex[...]` when there is ambiguity.

### Orthogonal / Cartesian Gather

Sometimes each index array should create its own output axis instead of zipping
with the others. Use an explicit orthogonal indexer on the receiver:

```python
batch_ids: array[i32] = [3, 0, 3, 1]
head_ids: array[i32] = [0, 2, 4]
hot_vocab: array[i32] = [2, 3, 5, 8, 13, 21]

cartesian = logits.oindex[batch_ids, :, head_ids, hot_vocab]
# tensor[f32][4, 8, 3, 6]
```

The semantic requirement is not tentative: Dudu needs a clear way to
distinguish pairwise gather from orthogonal/cartesian gather.

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
logits[:, :, :, :] += per_head_bias[newaxis, newaxis, :, newaxis]
```

Rules:

- `...` preserves all unspecified middle axes.
- negative indices count from the end.
- `newaxis` inserts a size-1 broadcast axis and should not allocate.

### Broadcasting

Broadcasting should be library/type-driven, but the target syntax needs to be
clear:

```python
mean: tensor[f32][784]
inv_std: tensor[f32][784]
x: tensor[f32][32, 784]

x_norm = (x - mean[newaxis, :]) * inv_std[newaxis, :]
# tensor[f32][32, 784]

b1: tensor[f32][256]
h1 = matmul(x_norm, w1) + b1[newaxis, :]
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

x = token_embedding.vindex[token_ids, :]  # tensor[f32][4, 16, 512]
x += position_embedding[newaxis, :, :]

qkv = einsum("btc,chd->bthd", x, wqkv)  # tensor[f32][4, 16, 3, 8, 64]
q = qkv[:, :, 0, :, :]                  # tensor[f32][4, 16, 8, 64]
k = qkv[:, :, 1, :, :]                  # tensor[f32][4, 16, 8, 64]
v = qkv[:, :, 2, :, :]                  # tensor[f32][4, 16, 8, 64]

scores[:, :, :, not valid_token] = -inf[f32]()
```

And a more grounded MLP/MNIST-style case:

```python
important_features: array[i32] = [0, 1, 2, 27, 28, 29, 391, 392, 393]
diagnostic_inputs = x_norm.oindex[:, important_features]  # tensor[f32][32, 9]

train_x = x_norm[train_row, :]                          # tensor[f32][dyn, 784]
correct_class_logit = logits.vindex[arange[32](), label] # tensor[f32][32]
nll = -log_probs.vindex[train_row, train_label]          # tensor[f32][dyn]
```

These examples should become tracked fixtures/examples as the syntax becomes
real. They do not belong in the fast default test suite.

## Implementation Phases

### 1. Audit Current Indexing Hooks

Verify the current compiler can express the library surface we need:

- `@operator("[]")` with multiple scalar indices
- `@operator("[]=")` with multiple scalar indices plus value
- method calls on indexed member receivers
- return types that are scalar, reference-like, view-like, or wrapper values
- slice arguments passed to library hooks, not only Dudu fixed arrays
- pairwise gather forms rejected or lowered deliberately
- orthogonal gather marker syntax decided or rejected with a clear diagnostic
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
- Done: operator declarations now validate `[]` / `[]=` arity directly.
- Done: `.vindex[...]` and `.oindex[...]` dispatch to library-owned
  `@operator("vindex[]")` / `@operator("vindex[]=")` and
  `@operator("oindex[]")` / `@operator("oindex[]=")` hooks. This gives
  libraries explicit pairwise and orthogonal gather/scatter entry points
  without compiler tensor-backend knowledge.
- Done: Dudu class receivers without matching index hooks now diagnose missing
  `@operator("[]")` or `@operator("[]=")` directly instead of reporting
  "cannot index non-container".
- Covered by fixtures: `tests/fixtures/tensor_multi_index_hook.dd` and
  `tests/fixtures/tensor_slice_hook.dd` and
  `tests/fixtures/tensor_slice_views.dd` and `tests/fixtures/cpu_tensor_matmul.dd`
  and `tests/fixtures/tensor_vindex_hook.dd` and
  `tests/fixtures/tensor_oindex_hook.dd` and `tests/fixtures/bad_tensor_vindex.dd` and
  `tests/fixtures/bad_tensor_oindex.dd` and
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
- Done: `assume_shape[T](value)` lets library/user code narrow a runtime-known
  `dyn` shaped value to a concrete shaped type after explicit runtime checks.
  The builtin is restricted to shaped target types and lowers to the value
  expression, so it carries only type metadata and does not add hidden tensor
  copies or backend behavior.
- Done: generic functions can use shape parameters in library tensor metadata,
  such as `Tensor[f32][Rows, Inner]`, and propagate composed result shapes like
  matrix multiply without leaking erased shape-only parameters into generated
  C++ templates. Conflicting shape inference is diagnosed in Dudu source.
- Remaining: advanced mask semantics, repeated-index scatter policy, richer
  view objects and broader propagation of shape facts through composed tensor
  expressions.

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
  add/sub/mul, `scale`, transpose, reductions, mean squared error,
  callback-based `map_values`, `relu`, row-bias add, and pure Dudu matmul.
- `tests/fixtures/tensor_dogfood/views_main.dd` validates the reusable module's
  matrix/image-style view behavior through normal imports.
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
- Remaining: add broader activation/map helpers as needed by later examples.

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
