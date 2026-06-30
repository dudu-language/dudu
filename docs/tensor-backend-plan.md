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

## Fancy Indexing Target Forms

The scratch dogfood file
`/home/vega/Coding/Web/dudu-webserver/indexingcoolness.dd` sketches the kind of
indexing Dudu should eventually make pleasant. The compiler plan should use
those forms as target cases instead of only saying "NumPy-like indexing".

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

Integer index arrays with matching shape select pairwise coordinates. This is
the important "get the selected class/logit for each row" form:

```python
label: tensor[i32][32]
logits: tensor[f32][32, 10]

correct_class_logit: tensor[f32][32] = logits[arange[32](), label]
```

Transformer-style token embedding lookup is the same idea at higher rank:

```python
token_ids: tensor[i32][4, 16]
token_embedding: tensor[f32][32000, 512]

x: tensor[f32][4, 16, 512] = token_embedding[token_ids, :]
```

Rule: pairwise gather is not a view. It creates a gather expression or owning
result unless the backend can represent a lazy gather view.

### Orthogonal / Cartesian Gather

Sometimes each index array should create its own output axis instead of zipping
with the others. The current sketch uses `o[...]` as an orthogonal marker:

```python
batch_ids: array[i32] = [3, 0, 3, 1]
head_ids: array[i32] = [0, 2, 4]
hot_vocab: array[i32] = [2, 3, 5, 8, 13, 21]

cartesian: tensor[f32][4, 8, 3, 6] =
    logits[o[batch_ids], :, o[head_ids], o[hot_vocab]]
```

This spelling is tentative, but the semantic requirement is not: Dudu needs a
way to distinguish pairwise gather from orthogonal/cartesian gather.

### Boolean Masks

Boolean masks should select rows/elements and produce dynamic-size axes when
the selected count is runtime-known:

```python
train_row: mask[bool][32]
x_norm: tensor[f32][32, 784]
label: tensor[i32][32]

train_x: tensor[f32][?, 784] = x_norm[train_row, :]
train_label: tensor[i32][?] = label[train_row]
```

Computed masks should work the same:

```python
visible: mask[bool][4, 16, 16] = frames[:, :, :, 3] > 0.0
visible_pixels: tensor[f32][?, 4] = frames[visible, :]
```

Rule: `?` is a runtime-known dimension. It must be explicit in type displays
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

x_norm: tensor[f32][32, 784] = (x - mean[newaxis, :]) * inv_std[newaxis, :]

b1: tensor[f32][256]
h1: tensor[f32][32, 256] = matmul(x_norm, w1) + b1[newaxis, :]
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

x: tensor[f32][4, 16, 512] = token_embedding[token_ids, :]
x += position_embedding[newaxis, :, :]

qkv: tensor[f32][4, 16, 3, 8, 64] = einsum("btc,chd->bthd", x, wqkv)
q: tensor[f32][4, 16, 8, 64] = qkv[:, :, 0, :, :]
k: tensor[f32][4, 16, 8, 64] = qkv[:, :, 1, :, :]
v: tensor[f32][4, 16, 8, 64] = qkv[:, :, 2, :, :]

scores[:, :, :, not valid_token] = -inf[f32]()
```

And a more grounded MLP/MNIST-style case:

```python
important_features: array[i32] = [0, 1, 2, 27, 28, 29, 391, 392, 393]
diagnostic_inputs: tensor[f32][32, 9] = x_norm[:, important_features]

train_x: tensor[f32][?, 784] = x_norm[train_row, :]
correct_class_logit: tensor[f32][32] = logits[arange[32](), label]
nll: tensor[f32][?] = -log_probs[train_row, train_label]
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
