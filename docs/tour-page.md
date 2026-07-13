# Dudu Language Tour

This document defines the argument and content for `dudulang.org/tour.html`.
The page is a first-person explanation of why Dudu exists, not a generic
feature matrix and not a claim that every other language is bad.

## Core Argument

The short version is:

> The gist of it is I wanted C+- but with Python syntax. So I made that. It is
> nice.

Dudu exists at the intersection of several useful but frustrating languages:

- Python has excellent muscle memory and readable everyday syntax, but CPython
  is too slow and memory-heavy for many native, embedded, graphics, and systems
  programs. The language has also accumulated syntax and a typing culture that
  can make simple code feel less simple without giving runtime guarantees.
- C has a direct memory model, is portable, and has relatively few forms that
  encourage clever code. It lacks operator overloading, which is useful for
  graphics and game development. Header files are unpleasant to work with, and
  its selection constructs are weaker than the match form wanted for Dudu.
- Rust has useful enums, matching, results, tooling, and compile-time checks.
  Its ownership semantics can be more annoying than helpful. Traits and `dyn`
  are useful in narrow cases but can encourage unnecessary cleverness and
  footguns. C and C++ libraries cross an FFI boundary, often involving
  conversion or copying. They need Rust-facing bindings, which creates a
  bootstrapping and walled-garden problem.
- C++ has a great ecosystem and libraries, mature tools, operator overloading,
  and nearly every feature someone could want. It can be hard to read
  sometimes, and it is slow to write without established C++ muscle memory.
  Boilerplate and declaration noise can distract from the program logic,
  making equivalent Python-shaped code easier to hold in your head.

Dudu keeps Python's familiar source shape while choosing static types,
predictable native data, direct C/C++ interoperability, and readable C++20 as
its implementation path. It is not Python-compatible and it does not try to
hide that it is a systems language.

## Tone

The page should be dry, direct, and personal. It can be opinionated. It should
not read like startup copy or pretend subjective language preferences are
universal facts. Do not add mood-setting kickers, slogans, metaphors, cute
transitions, or claims that exist only to sell the language. Headings should
name the actual subject of the section.

Use first person for motivation and plural/project voice for concrete compiler
behavior. Avoid presenting normal expectations such as an LSP, formatter, or
diagnostics as headline language features. Those belong in tooling notes.

## Page Structure

The page uses a full-width article without a persistent section sidebar. Keep
section IDs so headings remain directly linkable, but reserve the horizontal
space for two- and three-way code comparisons.

### 1. Why

Explain the four-language motivation above, ending with the C+-in-Python-shape
summary.

### 2. Dudu Looks Like Python

Show paired Python and Dudu snippets for:

- ordinary functions and control flow
- classes and aggregate construction
- modules and imports

The point is muscle memory, not source compatibility.

### 3. Type Inference Without Annotation Noise

Make it explicit that Dudu is statically typed without requiring an annotation
on every binding. Show the same substantial block twice: first with local types
written everywhere, then in normal Dudu with call results, constructors,
operators, indexed values, loop variables, generic substitutions, and non-empty
container literals inferred.

Explain where a written type contributes information:

- function parameters and value returns define API contracts
- public fields define layout and interfaces
- empty containers need an element type
- uninitialized fixed storage needs both element type and extent
- pointers, references, fixed-width ABI values, and native layout choices should
  be explicit when context does not already constrain them
- compile-time tensor extents are optional contracts

Mention that the editor can show inferred types as inlay hints without putting
them in source. Do not imply that explicit and inferred local bindings have
different runtime behavior when they resolve to the same type.

### 4. Deliberately Simpler Than Python

Show concrete omissions and substitutions:

- aggregate classes get construction without `@dataclass`
- operators use visible `@operator(...)`, not reserved dunder names
- annotations are checked by the compiler rather than stored as optional
  runtime metadata
- core types use fixed-width names rather than ambiguous `int` and `float`
- no `lambda`, comprehensions, generators, ternary expressions, or `with`
- no core `async`/`await` syntax; use native threads, event loops, callbacks,
  nonblocking APIs, or an imported C++ coroutine/task library

This section should explain the tradeoff: Dudu is more explicit and sometimes
longer. That is intentional. It prioritizes readable control flow over compact
expression tricks.

Also show dynamic Python behavior that Dudu rejects:

- rebinding a variable to an unrelated type
- homogeneous Python and Dudu lists as the normal case, followed by a separate
  heterogeneous Python list and Dudu payload-enum list comparison
- adding fields or methods to classes at runtime
- runtime reflection or monkey-patching as ordinary control flow

These are not bugs in Python. They are outside Dudu's static native model.

Include a deliberately complicated Python `typing.Callable` annotation beside
the equivalent Dudu `fn(...) -> ...` type. The comparison should use the same
data model on both sides rather than simplifying the problem only for Dudu.

Follow it with a separate type-heavy example. The Python side should show
ordinary forward references expressed as strings, including `TYPE_CHECKING`
imports and repeated quoted annotations. The Dudu side should use the same
domain types directly. This is distinct from the nested callable example: its
purpose is to show annotation noise, not difficult application logic.

The runtime-type comparison must define the same class on both sides. Show
Python adding a class method, adding an instance field, and replacing a method.
Show the same three attempted mutations in Dudu with red inline compiler errors.

Include a Python class with several dunder protocols beside the corresponding
Dudu class. Dudu should use aggregate fields, ordinary named methods, and only
the explicit `@operator(...)` declarations that are actually needed.

### 5. Native Runtime And Memory

Show a small scalar loop in Dudu and Python and a dynamic-list example.

Performance claims must be reproducible and scoped:

- identify the CPU, Python version, Dudu version, optimization flags, input
  size, and sample count
- use the existing `scripts/bench.sh` Dudu/C++ comparison
- use `scripts/bench_python.sh` for the CPython runtime and peak-RSS comparison
- describe Python numbers as CPython measurements, not all Python
  implementations
- do not imply every workload gets the same speedup
- explain that Dudu's durable claim is C++-class code generation, not a fixed
  multiplier over Python

The July 2026 local reference run used:

- AMD Ryzen 9 9950X
- CPython 3.12.3
- Dudu 0.1.0-alpha.13
- C++20, `-O3 -DNDEBUG`
- 10,000,000 elements/iterations, five calls per process

Observed values:

| Case | CPython | Dudu | C++ | CPython RSS | Dudu RSS | C++ RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| scalar accumulation | 1.094 s | 0.00922 s | 0.00918 s | 12.0 MB | 3.3 MB | 3.6 MB |
| build and sum integer list | 1.732 s | 0.104 s | 0.106 s | 325 MB | 68.9 MB | 68.7 MB |
| construct and update 500,000 particles for 20 steps | 0.407 s | 0.00457 s | 0.00468 s | 78.2 MB | 11.6 MB | 11.6 MB |
| 16 CPU-bound workers, 10,000,000 iterations each | 3.698 s | 0.00270 s | 0.00235 s | 11.8 MB | 4.1 MB | 4.1 MB |

The matching Dudu/C++ suite covered scalar loops, pointers, field access, fixed
arrays, vectors, tuples, and callbacks. Median Dudu/native ratios ranged from
0.97x to 1.05x in that run. These are microbenchmarks, not application-wide
promises.

Every performance case uses the same layout:

- CPython source on the left
- Dudu source in the middle
- equivalent handwritten C++ source on the right
- a runtime and peak-RSS table immediately below the three implementations

The page should show source for more than the scalar loop. At minimum include:

- scalar integer accumulation
- building and summing a large integer list, including peak RSS
- updating a large contiguous particle array, including runtime and peak RSS
- CPU-bound threading, showing CPython's GIL behavior beside native threads

Each comparison must have a checked-in benchmark input and command. Choose
cases that expose CPython object/interpreter costs, but do not describe them as
representative of every Python workload.

### 6. More Places To Run

Explain the architectural point without claiming finished platform support:
Dudu emits C++ and can follow a suitable native toolchain to desktop, WebAssembly,
embedded, or alternate CPU targets. Show conceptual deployment cards for:

- WebAssembly via a C++/Emscripten toolchain
- Arduino or another embedded C++ environment
- RISC-V via a matching cross compiler

Each card must label its status honestly. A possible toolchain path is not the
same thing as a validated distribution target.

### 7. Less Cute Code

Explain the intentional rejection of expression-heavy Python forms. Use the
real maintenance story: source-to-source tools converting loops into nested
comprehensions, then developers converting them back to edit them, is not a
win. Show a nested comprehension beside an explicit Dudu loop.

This section is primarily A/B examples, not prose. Include separate pairs for:

- a list comprehension and explicit list-building loop
- a dict comprehension and explicit dict-building loop
- a lambda callback and a named first-class Dudu function
- a Python generator and an explicit Dudu callback/output form
- a ternary expression and an ordinary typed local plus `if`/`else`
- a `with` statement and a scope-owned RAII guard from an imported C++ library

Explain the lambda decision directly: functions are first-class values in
Dudu, so named function declarations cover callback use cases without adding a
second anonymous function syntax.

### 8. Compared With C++

Show paired snippets for:

- inference without repeatedly spelling `auto`
- a larger block where C++ repeats `auto` for every inferred binding and Dudu
  simply uses ordinary assignments
- aggregate classes and construction
- operator overload declarations
- direct C and C++ imports, with examples progressing from a C function through
  C++ standard-library containers/algorithms, templates, and a third-party C++
  library
- inspectable layout and generated C++

State the performance goal precisely: Dudu should lower to the same native
operations a competent C++ implementation would use. The benchmark harness
compares equivalent implementations and should stay part of release
validation.

### 9. Native C And C++ Libraries

Give direct native use its own full section rather than treating it as one
small feature card. State that imports are scanned from real headers and retain
types, overloads, templates, macros, namespaces, source locations, and native
documentation. Native SDKs and link configuration still belong in the normal
CMake project.

Include concise but real examples for at least:

- the C++ standard library
- SDL3
- raylib
- Vulkan
- OpenCV
- SQLite
- FFmpeg
- libcurl
- Zstandard
- Dear ImGui
- GLM
- Boost.Asio

These should be representative of the compatibility suite, not invented claims
that every version and macro form has been validated.

Prefer direct imports for C APIs that already prefix every public symbol, such
as `sqlite3_*`, `curl_*`, `av_*`, `vk*`, `ZSTD_*`, and `cublas*`. Writing
`curl.curl_easy_perform` adds noise without preventing a collision the C API
has not already handled. Keep `as` available for real collision control and
for libraries whose unprefixed names benefit from a namespace.

### 10. Array And Tensor Indexing

Give numeric indexing its own full-width section. This is a language surface,
not a claim that Dudu ships NumPy or PyTorch in the standard library.

Start with fixed Dudu arrays and show that literal shapes are inferred:

```python
image: array[u8] = [
    [[255, 0, 0], [0, 255, 0]],
    [[0, 0, 255], [255, 255, 255]],
]
```

The inferred type is `array[u8][2, 2, 3]`. Show scalar indexing, ordinary
slices, stepped slices, multidimensional patches, ellipsis, and new axes:

```python
blue = image[1, 0, 2]
red = image[:, :, 0]
patch = image[0:2, 0:2, :]
every_other = samples[0:100:2]
last_channel = activations[..., -1]
row_bias = bias[None, :]
```

Explain that a scalar-only index which consumes the rank yields an element.
Any slice, ellipsis, or new axis yields an `array_view[T][result_shape]`. The
compiler computes the result shape per axis rather than using image-, matrix-,
row-, or column-specific code.

Then show the same syntax dispatching through a library type. Include examples
from normal ML code:

```python
train_x = x[mask, :]
correct = logits[arange(batch), labels]
token_vectors = embeddings[token_ids, :]
last_vocab = scores[..., -1]
normalized = (x - mean[None, :]) * inv_std[None, :]
weights[mask, :] = 0.0
logits[rows, cols] += 1.0
```

Show how a library defines the behavior with normal operators rather than a
compiler-known tensor name:

```python
class Tensor[T]:
    @operator("[]")
    def at[Idx...](self, *idx: Idx) -> TensorSelection[T, Idx...]:
        ...

    @operator("[]=")
    def set_at[Idx...](self, *idx: Idx, value: TensorAssignable[T, Idx...]):
        ...
```

The language owns parsing, typed index items, overload resolution, and generic
dispatch. The library owns view versus copy behavior, masks, gathers, scatter,
broadcasting, storage, autograd, BLAS, and GPU execution.

Show compile-time extents separately and describe them as optional. A library
may use ordinary runtime-shaped `Tensor[T]` values everywhere. Static extents
exist so model, kernel, buffer, and API boundaries can reject pipeline mistakes
during compilation instead of during a training run. Runtime-shaped and
statically shaped values must coexist.

The important ergonomic point is that callers do not repeat dimensions already
present in argument types:

```python
def matmul[M, K, N](
    left: Tensor[f32][M, K],
    right: Tensor[f32][K, N],
) -> Tensor[f32][M, N]:
    ...

left: Tensor[f32][32, 784]
right: Tensor[f32][784, 10]
logits = matmul(left, right)
# inferred: Tensor[f32][32, 10]
```

Include the convolution example because it proves extent arithmetic, generic
inference, loop bounds, and result checking together:

```python
def conv2d[H, W, K](
    image: &array[f32][H, W],
    kernel: &array[f32][K, K],
) -> array[f32][H - K + 1, W - K + 1]:
    ...

image: array[f32][4, 4]
kernel: array[f32][3, 3]
output = conv2d(image, kernel)
# inferred: array[f32][2, 2]
```

Explain `dyn` for dimensions known only at runtime and the explicit
`assume_shape[...]` boundary used to narrow runtime metadata after checking it.
Shape mismatch examples should show the actual style of Dudu diagnostic rather
than C++ template output.

End with an implementation table:

- implemented and covered by compiler fixtures: structured comma indexing,
  slices, ellipsis, `None` axes, scalar/index assignment, rank-independent
  fixed-array views, shape inference, symbolic extents, extent arithmetic,
  variadic `[]`/`[]=` hooks, shape diagnostics, and editor result types
- validated as library code: arbitrary-rank reference tensor storage, basic and
  advanced indexing, assignment/scatter, broadcasting, BLAS comparison, and
  OpenCL add/matmul probes
- not presented as production-ready: the reference `ndad`/`mald` code, a
  complete NumPy/PyTorch-compatible public library, packaged BLAS/GPU backends,
  or every backend's view/copy/autograd policy

### 11. Machine Learning Examples

Give ML its own large section. Keep the boundary between language support,
library surface, and backend implementation explicit.

Show:

- a normal two-layer network and training loop using runtime-shaped tensors
- a small scalar autograd value and reverse topological pass implemented as
  ordinary Dudu library code
- a Gemma-shaped decoder block with QKV projection, reshaping/transposes, RoPE,
  KV-cache indexed assignment, flash attention, gated MLP, and residuals
- a CUDA backend wrapper using `cuda_runtime_api.h` and `cublas_v2.h`, while the
  model call site remains backend-neutral
- PPO rollout collection across vector environments with indexed storage,
  masks, reshape, permutation gathers, and minibatch construction
- direct LibTorch use through `torch/torch.h`
- the honest JAX route: JAX is a Python frontend rather than a supported general
  C++ frontend, so show StableHLO export and native OpenXLA/PJRT execution rather
  than claiming Dudu can import JAX as a header library; note that raw StableHLO
  and custom calls require compatible runtime/version handling

The section should say that these examples demonstrate language expressiveness
and native integration. Dudu does not currently bundle the complete production
`mald`/`ddtorch`-style framework shown by the target surface.

### 12. Features From Other Languages

Show language forms adopted because they solve real problems:

- Rust-style payload enums
- exhaustive `match`
- `Result[T, E]` and `Option[T]`
- fixed-width scalar types
- fixed-shape arrays and Python-shaped indexing
- GLSL-style vector swizzling
- explicit references, pointers, and const qualification

These need A/B source examples too:

- Rust payload enum and `match` beside Dudu payload enum and `match`
- Rust `Result` handling beside Dudu `Result` handling
- nested C++ `std::array` fixed shapes beside Dudu `array[T][...]`
- a Dudu vector definition followed by `xyzw`, `rgba`, and assignment swizzles
- C or C++ selection/error forms where they clarify why the Dudu form exists

Swizzling is enabled by the vector's fields rather than a compiler-known class
name. A type with compatible `x/y/z/w`, `r/g/b/a`, or `s/t/p/q` components can
use swizzles. Reads may reorder or repeat components. Writes may reorder but
cannot repeat a destination component. Different-width reads use a matching
vector result type when one exists, such as `Vec4.xy -> Vec2`.

Mention that C++ exceptions remain available for native compatibility, while
normal Dudu APIs should prefer results where practical.

### 13. What Dudu Is Not

End with a compact boundary list:

- not a Python implementation
- not memory-safe by ownership/lifetime enforcement
- not a new runtime or package silo
- not production-stable yet
- not intended to replace every C++ construct

The closing action is to install Dudu, read the language guide, or inspect the
generated C++.

## Homepage Follow-Up

The homepage's language section should stop listing LSP/tooling as if it were a
language feature. Keep the homepage summary to language/runtime facts and link
to the Why page for the full argument.

The main navigation should expose `Why` as a first-class page.
