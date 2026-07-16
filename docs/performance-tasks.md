# Dudu Performance Tasks

This is the bounded performance backlog for the Dudu compiler, project driver,
language server, macro system, generated C++, and generated programs. It turns
the latency section of [Le Plan 2](leplan2.md#1-user-visible-latency) into
concrete work with stopping conditions.

Performance work must remain measurement-driven. Completing this document does
not mean making every number as small as possible. It means meeting the user-
visible budgets, removing pathological scaling, preserving native-quality
generated programs, and leaving evidence that future regressions can be found.

## Current Position

The July 15, 2026 reference measurements show:

- `duc check` on a tiny program: 20.9 ms
- Dudu-only no-op or changed-module emission: about 22 ms
- generated-CMake no-op build: 72.7 ms
- generated-CMake one-module edit: 331.8 ms, of which about 310 ms is native
  CMake, GCC, and linker work
- `raymarch-dd` cold workspace usable: 299.6 ms
- `dudu-webserver` cold workspace usable: 149.9 ms
- warm dogfood LSP requests: 6-21 ms
- 1,000-unit mixed Dudu frontend: 127.7 ms
- 1,000-unit Dudu C++ emission: 219.4 ms
- generated C++ compile/link: 256.4 ms, versus 264.8 ms for the equivalent
  handwritten C++ fixture
- complete self-contained Dudu toolchain: 477.4 ms
- 1,000-type macro project: 410.1 ms cold and 52.5 ms cached

The frontend and emitter scale approximately linearly on the current generated
fixtures. Previously quadratic class-symbol copying and dependency/member
lookup paths have been removed. Generated runtime support is feature-gated,
module imports no longer leak through the global runtime header, generated
sources consume their own module headers, and stable runtime support is
precompiled by generated-CMake builds.

Detailed measurements live in:

- [Performance](performance.md)
- [Compiler Performance Matrix](compiler-performance-matrix.md)
- [Macro Performance Matrix](macro-performance-matrix.md)
- [Macro Performance Plan](macro-performance-plan.md)
- [Generated C++ Boundary Plan](generated-cpp-boundary-plan.md)

## Rules

1. Profile before changing code. Keep before/after CSV or JSON and exact
   reproduction commands.
2. Separate lexing/parsing, semantic analysis, emission, native scanning,
   external compilation, linking, build orchestration, macros, and LSP time.
3. Use diverse generated shapes and real projects. A result from one repeated
   declaration pattern is not a general compiler result.
4. Test scaling at several sizes. Any superlinear curve needs an explanation.
5. Prefer removing repeated work over hiding it behind a cache. Every cache
   needs stable identities, invalidation tests, and observable hit/miss state.
6. Preserve diagnostics, invalid-edit recovery, readable generated C++, source
   mapping, and native correctness.
7. Keep heavyweight benchmarks explicit and out of the normal fast test loop.
8. Stop when a tracked case meets its budget unless profiling shows a genuine
   architectural defect.

## P0: Remaining User-Visible Latency

These are the performance tasks worth addressing before broad optimization.

### 1. Keep Cold Macro Package Compilation Bounded

Status: complete for the current architecture. Generated macro modules compile
as independent C++ translation units in parallel with the catalog/dispatch
worker, which includes their headers instead of textual `.cpp` bodies. This
reduced package compilation from 364.0 ms to 257.3 ms and the complete cold
1,000-type project from 514.5 ms to 410.1 ms median. Cached expansion remains
52.5 ms.

Required work:

- retain a stable prebuilt generic launcher/package ABI as an option only if a
  larger measured package cannot meet the budget through separate compilation
- build compatible SDK artifacts during toolchain installation
- compile only package-specific macro implementation and small dispatch glue
  when unavoidable
- keep package and toolchain identities content-addressed
- test toolchain upgrades, stale artifacts, failed expansion, and concurrent
  package builds
- compare cold and cached Dudu macro workloads with Rust, C#, Swift, and Nim

Completion:

- 1,000-type cold project under 500 ms on the reference machine
- cached project under 100 ms
- no correctness or invalidation dependence on timestamps alone

The current five-sample Release result meets this completion gate. Do not add a
dynamic package/plugin ABI solely to reduce a path that is already within
budget.

### 2. Harden Cold Native Indexing And Metadata Caching

Current small-header results are good, but large template-heavy include graphs
can still make Clang work dominate compilation and editor startup.

Required work:

- count Clang invocations and prove one scan per required native identity
- profile preprocessing, AST traversal, metadata serialization,
  deserialization, dependency validation, and graph merge separately
- avoid loading metadata not reachable from the current module graph
- make cache keys include compiler, target, language mode, flags, include
  paths, defines, and transitive dependency state
- test GCC/libstdc++ and Clang/libc++ spelling and invalidation differences
- add bounded fixtures for template-heavy STL, SDL/raylib, and one larger
  native include graph

Completion:

- cached native metadata under 100 ms
- cold dogfood workspace usable under 1 second
- no stale symbol, hover, definition, or overload information after a header
  or build-flag change

### 3. Keep LSP Requests Independent Of Cold Work

The measured dogfood workspaces meet the current targets. Preserve that result
as projects grow.

Required work:

- keep one shared `ProjectIndex` and native metadata view per workspace
- perform cold indexing in the background without blocking unrelated syntax,
  diagnostics, completion, or navigation
- retain useful partial AST and semantic information during invalid edits
- benchmark first diagnostics, first native request, and warm hover,
  definition, references, rename, completion, signature help, inlay hints, and
  semantic tokens independently
- measure cancellation and rapid-edit behavior, not only isolated requests
- add 10k, 50k, and multi-module editor fixtures in addition to dogfood repos

Completion:

- cold workspace usable under 1 second
- warm hover/definition under 50 ms
- warm references/semantic tokens under 100 ms
- invalid source never causes a long synchronous re-index or removes all
  recoverable editor intelligence

### 4. Bound Project-Driver And Native Build Overhead

Dudu's changed-module work is already about 22 ms. Most of the 332 ms edit
cycle belongs to native build tools, but the driver must not invoke them when
nothing changed.

Required work:

- prove no-op `check`, `build`, `run`, and `test` avoid unnecessary analysis,
  emission, configure, compile, and link phases
- preserve generated file mtimes when content is unchanged
- benchmark one private implementation edit, one public interface edit, one
  native-header edit, one build-config edit, and one dependency edit
- expose phase timings and cache/invalidation decisions under `--timings`
- avoid relinking when the selected native backend can prove it unnecessary
- preserve normal CMake/compiler output whenever those tools run

Completion:

- no-op build under 250 ms
- one-module Dudu analyze/emit under 200 ms
- every invoked native phase has an observable reason

## P1: Compiler Scaling And Generated Output

These tasks guard larger programs. They are not reasons to delay native
correctness or diagnostics when current budgets remain green.

### 5. Broaden Frontend Scaling Fixtures

The benchmark suite must cover more than repeated classes.

Add generated and mixed fixtures for:

- declarations, scopes, locals, and long functions
- expression and overload-heavy code
- classes, inheritance, abstract dispatch, and operators
- type and value generics, const arithmetic, and instantiation fan-out
- imports, deep and wide module graphs, and public/private dependencies
- native declarations and overload sets
- arrays, slices, shaped types, indexing packs, and match patterns
- valid programs, many independent errors, and deeply malformed edits
- declaration macros and generated declarations

Measure 10k, 50k, 100k, 250k, 500k, and 1M source-line equivalents where the
fixture remains meaningful. Record wall time, CPU time, peak RSS, node/symbol
counts, and output size.

Completion:

- no unexplained superlinear curve
- no whole-symbol-table, whole-module, or whole-graph copy in an inner loop
- memory growth is approximately linear for bounded semantic workloads

### 6. Profile Ordinary C++ Emission

The known quadratic emitter paths are fixed. The remaining difference between
frontend checking and complete emission is about 92 ms on the 1,000-unit
fixture.

Required work:

- profile type lowering, expression lowering, dependency lookup, source-map
  generation, string formatting, allocation, and output writes separately
- use indexed symbol/type identities instead of repeated rendered-name lookup
- reserve output only from measured size estimates
- stream or buffer by module without constructing redundant whole-project
  strings
- rerun diverse fixtures after every structural optimization

Completion:

- emission remains linear through the largest bounded fixtures
- no repeated parsing or semantic reconstruction from rendered C++ text
- optimization stops when emission is not material in dogfood edit latency

### 7. Keep Generated Native Dependencies Precise

The global runtime-header native-import leak is fixed. Prevent equivalent
problems from returning at narrower boundaries.

Required work:

- preserve feature-selected runtime includes and support definitions
- preserve module-local ownership of native imports
- distinguish imports required by public declarations from source-only imports
- ensure unrelated modules do not inherit SDL, raylib, database, GPU, or other
  native headers
- audit opaque `cpp(...)` in header-owned generic/constexpr bodies; remain
  conservatively module-local unless explicit metadata can improve precision
- test include cycles, transitive public dependencies, and native header edits

Completion:

- generated headers are self-contained
- every emitted native include has a module, public-interface, or selected
  runtime reason
- no library-name-specific compiler branch

### 8. Control Generated C++ And Binary Size

Generated source is intentionally more verbose than compact handwritten C++,
but it must not produce pathological parser, debug-info, object, or executable
growth.

Required work:

- track source lines/bytes, preprocessed tokens, object size, debug size, and
  final binary size
- make inspectable source-location comments independently configurable while
  preserving machine source maps
- remove declarations only when dependency analysis proves them unnecessary
- check template instantiation duplication and identical support definitions
  across modules
- compare Debug and Release output with equivalent C++

Completion:

- output growth has an explained structural source
- no duplicated runtime/native support across translation units
- generated-program debug and release sizes remain in the same practical class
  as equivalent C++

### 9. Evaluate Frontend Representation Only With Evidence

An arena/node-id or compact variant AST may improve locality and peak memory,
but changing the representation is not automatically an optimization.

Required work before adoption:

- profile allocation count, AST bytes, semantic-fact bytes, cache locality, and
  destruction time
- prototype against the same parser, sema, diagnostics, and LSP behavior
- measure large valid, error-heavy, and editor-recovery workloads
- reject designs that introduce dual ASTs, compatibility mirrors, or string
  semantic paths

Completion:

- adopt only with broad measured latency or memory improvement and simpler
  ownership
- otherwise retain the current structured AST and fix local hot paths

## P1: Generated-Program Performance

Compiler speed is not enough. Dudu promises native code in the C++ performance
class.

### 10. Build A Runtime And Code-Quality Matrix

Add Dudu and equivalent C++ benchmarks for:

- scalar integer and floating-point loops
- function calls, methods, virtual dispatch, and operator overloads
- value, pointer, mutable-reference, and read-only-reference passing
- array iteration, indexing, slicing/view construction, and swizzles
- containers, allocation, move/copy behavior, and destruction
- payload enums, `Option`, `Result`, `variant`, and exhaustive match
- generics and value-generic specialization
- threads, atomics, SIMD intrinsics, and auto-vectorizable loops
- C and C++ interop calls

Record runtime, allocations, peak RSS, binary size, and selected assembly/LLVM
or GCC optimization reports. Compare equivalent semantics and optimization
flags; do not compare a checked Dudu operation with an unchecked C++ one
without labeling the difference.

Completion:

- ordinary zero-cost forms match equivalent C++ within benchmark noise
- every material difference is documented as an intentional check/runtime
  feature or fixed as a lowering defect
- no hidden allocation, copy, heap indirection, or dynamic dispatch caused by
  surface syntax alone

### 11. Guard Compile-Time Features Against Runtime Cost

Verify that inference, static shapes, const arithmetic, declaration macros,
and semantic metadata disappear from optimized programs unless explicitly
represented by a library type.

Completion:

- compile-time-only features add no runtime storage or branches
- generated code and assembly fixtures catch regressions

## P2: Useful Follow-Up Work

Do these when measurements or user reports justify them:

- parallel parsing/analysis/emission for large independent module graphs
- persistent compiler daemon experiments, only if process startup becomes
  material after simpler fixes
- alternative linker/compiler launch configuration without hiding native tool
  output or changing semantics
- faster dependency download/extraction and content-addressed package reuse
- formatter throughput on very large workspaces
- install, update, rollback, and extension startup measurements
- profile-guided optimization or link-time optimization for Dudu's own release
  binaries
- platform-specific measurements on maintained macOS and Windows machines

These are explicitly not current default work. Parallelism and daemons add
state, invalidation, shutdown, and debugging complexity and should not conceal
avoidable repeated work.

## Regression Infrastructure

The repository should provide:

- one command for the fast performance smoke set
- one explicit full compiler/LSP/macro/runtime matrix command
- machine-readable baselines with toolchain and hardware identities
- threshold checks broad enough to catch regressions without failing on normal
  scheduler noise
- trend comparison against a chosen commit or tagged release
- generated fixtures with fixed seeds and checked semantic output
- dogfood measurements for `raymarch-dd` and `dudu-webserver`

Ordinary pushes do not wait on hosted performance jobs. Run the bounded suite
locally at performance milestones and before releases. Hosted machines may
collect supplemental platform data, but they are not the optimization loop.

## Priority Relative To The Language Roadmap

Performance is no longer the primary obstacle to a post-alpha Dudu release.
The current frontend, incremental emission, build, LSP, and generated C++
results are adequate for alpha dogfooding.

After the P0 items meet their budgets, prioritize:

1. template-heavy and overload-heavy native C++ compatibility
2. diagnostic and invalid-edit recovery quality
3. compiler architecture/readability cleanup encountered during real fixes
4. a real `serdd` package to drive the external-conformance decision
5. numeric-library and optional BLAS/GPU ecosystem proof
6. maintained-platform validation and distribution feedback

Do not hold those areas behind speculative emitter, allocator, daemon, or
parallel-compiler work.

## Completion Gate

This performance push is complete when:

- every P0 budget is met or has a measured external-tool explanation
- frontend and emission curves remain approximately linear across diverse
  bounded fixtures
- generated program performance has a C++ parity matrix
- macro cold/cached behavior meets its budget
- native metadata invalidation and LSP responsiveness remain correct
- no-op and changed-module builds prove minimal work
- all results are reproducible from documented local commands
- no optimization adds library-specific behavior, semantic string fallbacks,
  stale caches, or hidden native work

After that gate, performance work returns to regression-driven maintenance.
