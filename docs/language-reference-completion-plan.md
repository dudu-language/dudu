# Language Reference Completion Plan

This plan closes the compiler and documentation gaps found while reviewing the
public language guide. It covers collection inference, fixed-array inference,
compile-time programming, generics, native imports and macros, allocation, and
numeric indexing.

The work is ordered so the compiler behavior is correct before the public
documentation promises it. Each section must end with deterministic fixtures,
accurate user-facing documentation, and examples checked by the current Dudu
compiler.

Status: completed on 2026-07-13. All nine sections are implemented, validated,
linked from the public manual, and covered by the local site publication gate.

## Engineering Bar

- Do not document intended behavior as implemented behavior.
- Do not special-case example names, library names, tensor names, or container
  variable names in the compiler.
- Type inference must be represented by normal AST and semantic type facts.
- Compiler, codegen, LSP, formatter, and documentation examples must agree.
- Empty or ambiguous forms must produce a useful diagnostic instead of an
  unparameterized container that fails later.
- Plans and architecture notes are not substitutes for public reference pages.
- Every public code sample must either compile or be explicitly marked as a
  diagnostic example with its expected error.

## 1. Complete Collection Literal Inference

### Problem

Non-empty collection literals do not currently carry complete inferred generic
types through semantic analysis. The parser recognizes list, dict, and set
literals, but an unannotated local can retain only bare `list`, `dict`, or
`set`. Downstream indexing and member lookup can then fail even when the
literal's element types are obvious.

This is a compiler correctness gap. Documentation must not say that collection
literals infer their types until this behavior works end to end.

### Required semantics

```python
numbers = [1, 2, 3]                 # list[i32]
scores = {"ada": 20, "bob": 22}    # dict[str, i32]
names = {"ada", "bob"}             # set[str]
```

- A non-empty homogeneous list infers `list[T]`.
- A non-empty dict infers its key and value types independently as
  `dict[K, V]`.
- A non-empty homogeneous set infers `set[T]`.
- Nested literals infer recursively.
- Expected type context is propagated into literal elements.
- Empty list, dict, and set literals require an expected type or annotation.
- Incompatible elements produce a diagnostic at the conflicting element.
- Heterogeneous collections require an explicit `variant` element type.
- Integer literals default to `i32` and decimal literals default to `f64` only
  when no expected type supplies a width.

### Compiler work

- Add one shared collection-literal type inference path owned by semantic
  analysis.
- Return parameterized `TypeRef` values rather than bare container names.
- Reuse normal type compatibility and numeric-context rules while unifying
  elements.
- Record inferred local types in the same semantic facts consumed by codegen
  and the LSP.
- Reject empty untyped literals at the declaration, not at a later use.
- Remove emission-side guesses that duplicate semantic inference.

### Fixtures

- Positive list, nested-list, dict, nested-dict, and set inference.
- Contextual numeric inference into `list[f32]`, `dict[str, u64]`, and
  `set[usize]`.
- Indexing, iteration, method calls, returns, and function arguments using
  inferred containers.
- Empty literal diagnostics.
- Mixed-element diagnostics with an explicit `variant` success case.
- LSP hover and inlay hints showing complete inferred generic types.
- Generated C++ checks showing concrete `std::vector`, `std::map` or
  `std::unordered_map`, and `std::set` element types according to Dudu's
  canonical lowering.

### Acceptance

The three introductory examples compile without annotations, hover reports the
complete types, bad elements point at the conflicting value, and no compiler
phase receives a bare built-in collection type for a successfully inferred
local.

## 2. Lock Fixed-Array Literal Inference And Numeric Defaults

### Current behavior to preserve

```python
kernel: array[f32] = [
    [1.0, 0.0, -1.0],
    [1.0, 0.0, -1.0],
    [1.0, 0.0, -1.0],
]
```

The initializer determines the fixed shape, producing
`array[f32][3, 3]`. The annotation determines the storage family and scalar
width. A bare nested `[...]` literal remains a dynamic nested list rather than
silently becoming fixed contiguous storage.

### Required semantics

- `array[T] = non_empty_literal` infers all extents.
- `array[T][extents] = literal` checks the declared shape against the literal.
- The literal must be rectangular at every rank.
- An empty fixed array requires explicit extents.
- Expected element type context applies recursively, so decimal literals in an
  `array[f32]` are `f32` without individual casts.
- Unconstrained integers default to `i32`; unconstrained decimals default to
  `f64`.
- Dudu does not infer fixed-array storage from a bare list literal.

### Compiler and editor audit

- Ensure parser, sema, codegen, diagnostics, hover, and inlay hints all consume
  the same inferred shaped `TypeRef`.
- Test ranks one through at least four without rank-specific inference code.
- Diagnose ragged literals and shape mismatches at the first mismatching row or
  extent.
- Ensure inferred shape information survives function calls, returns, imports,
  and module boundaries.

### Documentation

Explain separately what is inferred and what remains explicit:

- storage family: explicit `array`
- element width: explicit `T` or supplied by expected context
- shape: inferred from a non-empty rectangular initializer

### Acceptance

The compiler, LSP, and generated C++ agree on the full type of shaped literals,
and the public guide does not imply that a bare Python list literal silently
chooses fixed native storage.

## 3. Build A Complete Compile-Time Programming Section

### Problem

The public docs currently show `@constexpr` briefly, even though compile-time
values affect constants, build configuration, static assertions, array extents,
and value-generic APIs.

### Required coverage

Document these as one coherent feature set:

1. All-caps compile-time constants.
2. `@constexpr` functions.
3. `static_assert`.
4. `sizeof[T]()` and `alignof[T]()` in constant expressions.
5. Build values from `dudu.toml` and `-D` arguments.
6. Compile-time-selected `if build.*` branches.
7. Fixed-array extents and value-generic expressions.
8. Diagnostics when a runtime value is used where a constant is required.

### Examples

Include examples that are useful outside toy arithmetic:

- packet layout and alignment checks
- fixed-capacity buffers
- platform/backend selection
- lookup-table sizing
- matrix and convolution output extents
- a failing static assertion with the expected diagnostic

### Verification

- Compile every positive sample.
- Keep negative samples as diagnostic fixtures.
- Verify generated C++ uses `constexpr`, `static_assert`, and `if constexpr`
  where required.
- Verify LSP hover distinguishes compile-time values from mutable runtime
  bindings.

### Acceptance

A reader can determine what is evaluated at compile time, what may be evaluated
at compile time, where compile-time evaluation is mandatory, and how build
configuration enters that environment.

## 4. Document Generics And Value-Generic Arithmetic Thoroughly

### Scope

Generics need their own user-facing progression:

- generic functions
- generic classes
- inferred type arguments
- explicit type arguments
- constraints supported by the language
- compile-time value parameters
- symbolic extent arithmetic
- imported C++ template use

### Required examples

```python
def clamp[T](value: T, low: T, high: T) -> T:
    ...

class SmallBuffer[T, N]:
    items: array[T][N]

def conv2d[H, W, K](
    image: &array[f32][H, W],
    kernel: &array[f32][K, K],
) -> array[f32][H - K + 1, W - K + 1]:
    ...
```

Explain that `H`, `W`, `K`, and `N` are compile-time `usize` values, not types.
Document supported arithmetic and normalization rules precisely. Show calls
where value arguments are inferred from shaped parameters instead of repeated
at the call site.

### Boundaries

- Do not describe this as unrestricted dependent typing.
- Do not imply support for arbitrary C++ template metaprogramming.
- State how `dyn` differs from a compile-time extent.
- State which relationships the compiler proves and which require runtime
  checks.

### Fixtures and editor support

- Positive arithmetic for `+`, `-`, `*`, `/`, `%`, and parentheses.
- Folded concrete results and preserved symbolic results.
- Invalid runtime extent use, unsupported expression, and shape mismatch
  diagnostics.
- Hover and inlay hints showing substituted and folded result shapes.
- Imported C++ type and non-type template calls remain covered separately.

### Acceptance

The guide demonstrates why value generics are useful, calls do not redundantly
repeat inferable extents, and diagnostics explain symbolic and concrete shape
mismatches in Dudu terms.

## 5. Publish The Complete Native Import Matrix

### Required syntax matrix

Show unaliased and aliased forms for system/header-search imports and
source-relative path imports:

```python
from c import SDL3/SDL.h
from c import SDL3/SDL.h as sdl
from c.path import vendor/foo.h
from c.path import vendor/foo.h as foo

from cpp import thread
from cpp import thread as threading
from cpp.path import vendor/math.hpp
from cpp.path import vendor/math.hpp as math
```

Document the equivalent `cxx` forms as well.

### Required explanation

- `c`, `cxx`, and `cpp` choose native scanning and linkage semantics.
- No `.path` uses normal compiler header search and emits an angle include.
- `.path` resolves relative to the importing source and emits a quoted include.
- No alias exposes real native globals and namespaces.
- `as` creates a hygienic collision boundary.
- Multiple C++ headers contributing to `std` or another common native namespace
  merge by canonical native identity.
- A collision between unrelated native and Dudu symbols is an error with an
  alias suggestion.
- Aliasing a header does not rename the native C++ declaration in generated
  code.

### Examples and editor behavior

- Direct prefixed C APIs such as SDL, SQLite, libcurl, FFmpeg, Vulkan, and
  Zstandard.
- Aliased C import used to contain genuinely broad globals.
- Unaliased C++ standard-library imports retaining `std`.
- Aliased local C++ vendor header.
- Ctrl-click behavior for language token, `path`, every header path segment,
  imported target, and alias.

### Acceptance

The website summary and [Import Semantics](import_semantics.md) agree, all
documented forms parse and compile, and no example teaches fake aliases such as
`from cpp import thread as std`.

## 6. Separate Generics, Imported Templates, And Macros

### Problem

The current public material mentions templates and macros without clearly
separating implemented language features, imported native behavior, and macro
design work.

### Required documentation model

Use four explicit categories:

1. **Dudu generics**: `def name[T]` and `class Name[T]`.
2. **Dudu value generics**: compile-time values such as `N`, `H`, and `W`.
3. **Imported C++ templates**: types and functions such as
   `std.vector[i32]`, `std.array[u8, 16]`, and explicit template calls.
4. **Imported C/C++ macros**: object-like, function-like, variadic, and
   lowercase callable macros discovered by native header scanning.

### Status honesty

- Imported native macros that form usable expressions or calls are supported.
- Token-pasting, stringizing, declaration-generating, and partial-syntax macros
  may require a native wrapper header and must be listed as native-boundary
  limitations.
- Dudu-defined additive declaration and derive macros are specified in
  [Dudu Macro System Plan](macro-syntax-plan.md), but are not a shipped public
  feature. Expression and control-flow macros are outside the accepted design.
- Compiler-known decorators such as `@operator` and `@constexpr` are not proof
  of a general user-defined macro system.

### Fixtures and examples

- Link to the native macro compatibility fixtures and macro-bomb test.
- Show one constant macro, one function-like macro, and one variadic macro.
- Show imported C++ class and function templates without wrappers.
- Ensure hover, completion, signature help, and definition navigation report
  native macro/template identity when scanner metadata exists.

### Acceptance

The docs never use "template" and "macro" interchangeably, and a reader can
tell exactly which forms are Dudu language features, native imports, or planned
syntax.

## 7. Expand Allocation And Custom Allocator Documentation

### Required correction

`new[T](...)` is function-shaped syntax but has compiler-defined allocation,
construction, and `delete` behavior. Do not describe it as an arbitrary ordinary
function.

Custom allocators are normal library APIs:

```python
enemy = arena.make[Enemy](100, 10.0, 20.0)
bytes = arena.alloc[u8](4096)
arena.reset()
```

The compiler does not assign ownership meaning to `arena.make`, `arena.alloc`,
or `arena.reset`.

### Required coverage

- value construction with `T(...)`
- heap construction with `new[T](...)` and `delete`
- raw storage with `malloc[T](count)` and `free`
- value-owning containers and RAII
- pointer containers that do not own pointees
- custom allocator and arena APIs
- placement construction and native allocator interop where supported
- fixed arrays versus dynamic owning lists
- escaping local references and other diagnosable lifetime mistakes

### Examples

- value-owned return from a function
- heap-owned object with explicit deletion
- C API allocation/free pairing
- arena reset lifetime boundary
- custom allocator returning an owning RAII value
- intentionally invalid return of a pointer/reference to a local

### Acceptance

The allocation guide is explicit about which operations have language-defined
meaning and which are library contracts, without implying Rust ownership or a
mandatory allocator model.

## 8. Create A Dedicated Arrays, Views, And Indexing Tutorial

### Purpose

The reference guide should list syntax. A dedicated tutorial should teach the
rank and view model well enough for readers who have not already learned NumPy,
PyTorch, Julia, or MATLAB indexing.

### Tutorial sequence

1. Fixed arrays, rank, extents, and row-major storage.
2. Scalar indexing and axis removal.
3. Slices and axis preservation.
4. Start, stop, and step forms.
5. `None` as a new axis.
6. `...` as the remaining axes.
7. Result-shape calculation, with diagrams or tables.
8. Views, ownership, strides, and contiguity.
9. Copy/materialization boundaries.
10. Indexed assignment and mutation.
11. Boolean masks, gathers, and library-defined advanced indexing.
12. Static extents versus runtime `dyn` extents.
13. Defining indexing on a library type with `@operator("[]")` and
    `@operator("[]=")`.
14. Native handoff to pointers, spans, BLAS, and GPU libraries.

### Documentation boundaries

- Built-in fixed-array behavior must be labeled as language behavior.
- Tensor, mask, gather, broadcast, and backend behavior supplied by a library
  must be labeled as library behavior.
- Do not claim that Dudu ships NumPy, PyTorch, `ndad`, or a GPU runtime as part
  of the language.
- Show whether every operation returns a scalar, view, or owning value.

### Validation

- Compile every built-in-array sample.
- Keep library samples in a checked dogfood package or clearly mark them as API
  targets.
- Test hover, inferred view shape, operator navigation, and bad-index
  diagnostics.
- Include arbitrary-rank examples so the tutorial does not imply a rank-2
  implementation.

### Acceptance

A reader can manually predict the result rank and shape of every tutorial
index, understands when storage is shared, and can see how an external numeric
library participates without compiler special cases.

## 9. Establish Canonical Public Reference Pages And Integrate The Site

### Problem

`import_semantics.md` is detailed enough to act as a canonical reference, while
several other public sections currently rely on summaries or implementation
plans. Public users should not need architecture roadmaps to learn stable
language behavior.

### Required public documents

Create or promote canonical user-facing references for:

- collections and literal inference
- fixed arrays and numeric literals
- compile-time programming
- generics and value generics
- native imports, templates, and macros
- memory, references, pointers, and allocation
- arrays, views, and indexing

Each document must contain:

- accepted syntax
- precise semantics
- diagnostics and invalid forms
- editor behavior where relevant
- native C++ lowering where it clarifies systems behavior
- tested examples
- explicit limitations

### Site integration

- Keep the main language guide concise and link each section to its canonical
  reference.
- Link the indexing tutorial from the guide, tour, and numeric examples.
- Do not expose implementation plans as the normal user path.
- Add previous/next navigation and stable heading anchors.
- Ensure site code highlighting recognizes every shown language.
- Run a code-sample validation script before deployment.
- Audit website claims against `known-limitations.md` and the compatibility
  matrix.

### Final audit

- Search the site for claims that container literals, macros, templates,
  allocators, or indexing work more broadly than tested.
- Search canonical docs for stale syntax.
- Confirm every internal link resolves.
- Confirm mobile and desktop layouts handle wide code examples without
  scrollbars crossing code content.
- Deploy only after local build, link, and sample validation passes.

### Acceptance

The public site is a coherent entry point into stable reference and tutorial
material, every advertised sample is validated, and implementation plans remain
engineering documents rather than accidental language manuals.

## Completion Order

Execute the numbered sections in order. Sections 1 and 2 establish compiler
truth. Sections 3 through 7 document and verify compile-time/native/system
behavior. Section 8 teaches the largest user-facing feature. Section 9 performs
the final reference extraction, site integration, validation, and deployment.

This plan is complete only when all nine sections meet their acceptance
criteria. A documentation-only workaround does not complete a compiler item,
and a passing compiler fixture does not complete a public documentation item.
