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

## Implementation Phases

### 1. Audit Current Indexing Hooks

Verify the current compiler can express the library surface we need:

- `@operator("[]")` with multiple scalar indices
- `@operator("[]=")` with multiple scalar indices plus value
- method calls on indexed member receivers
- return types that are scalar, reference-like, view-like, or wrapper values
- slice arguments passed to library hooks, not only Dudu fixed arrays
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
