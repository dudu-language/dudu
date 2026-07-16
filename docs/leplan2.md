# Le Plan 2

This is Dudu's active post-alpha engineering roadmap.

[Le Plan](le_plan.md) remains the detailed implementation history, design
record, and index of older feature plans. Do not execute it from the beginning
again. Use it when a task below references an existing subsystem or accepted
language decision.

## Current State

Dudu already has the core shape of a usable systems language:

- structured lexer, parser, AST, semantic analysis, and C++ emission
- canonical multi-file modules and separate generated C++ artifacts
- incremental module emission through the generated-CMake backend
- inference, classes, operators, generics, value generics, and const arithmetic
- fixed arrays, arbitrary-rank indexing syntax, slices, views, and swizzles
- payload enums, exhaustive matching, `Option`, `Result`, and `variant`
- common inheritance, abstract methods, virtual dispatch, and imported bases
- direct C/C++ imports with broad native-library coverage
- additive typed declaration macros through the public `dudu.ast` API
- path and Git dependencies with a lockfile
- formatter, LSP, VS Code extension, invalid-edit recovery, and native jumps
- Linux alpha installation, update, rollback, packages, and public docs

Do not reopen completed language decisions without a concrete failing program.
Do not add library-name special cases, compatibility syntax for an unreleased
language, rendered-string semantic paths, or temporary compiler shortcuts.

## Engineering Rules

- Profile before optimizing. Keep before/after machine-readable results.
- Test multiple generated shapes and real dogfood projects. A one-fixture win
  is not a compiler win.
- Separate frontend, native-header, generated-C++, linker, CMake, macro-worker,
  and LSP time. Do not report one aggregate number as an explanation.
- Keep routine validation local and targeted. Never make hosted CI the hot
  development loop.
- Keep heavyweight native and performance suites explicit, bounded, and out of
  the default fast loop.
- Prefer architecture changes that remove work over caches that conceal work.
  Caches still require deterministic identities, invalidation tests, and
  observable hit/miss reporting.
- Preserve diagnostics, generated C++ readability, correctness, and editor
  behavior while optimizing.
- Commit and push stable green milestones. Revert measured failures rather than
  accumulating speculative tuning.
- Update this file with current measurements and outcomes. Keep detailed
  experiment logs in the subsystem plan instead of growing this into another
  append-only history.

## 1. User-Visible Latency

This is the immediate priority.

The complete bounded backlog, generated-program parity work, benchmark rules,
and stopping conditions live in [Dudu Performance Tasks](performance-tasks.md).
Use that document for performance pushes instead of growing an unbounded list
of speculative optimizations here.

Pure Dudu frontend throughput is no longer uniformly slow. Recent results in
[Le Plan: Compiler Throughput](le_plan.md#11-compiler-throughput-and-build-performance)
put most 50,000-line generated shapes near 90-300 ms. The remaining bad latency
is concentrated in cold editor indexing, native-header work, process/build
orchestration, and native macro-package compilation. Generated-shape scaling
must still be measured because the first Le Plan 2 baseline caught a class
semantic-analysis path that looked acceptable at 10,000 lines but became
quadratic by 50,000 lines.

### Baseline

Measure Debug and Release builds on this machine for:

- compiler startup and tiny `duc check`
- `dudu check`, build, run, and test for no-op and one-file edits
- clean and incremental `raymarch-dd`
- clean and incremental `dudu-webserver`
- a native-header-heavy mini-project
- cold workspace indexing and first diagnostics
- warm hover, definition, references, completion, semantic tokens, and rename
- first native-aware editor request and repeated native-aware requests
- first macro SDK bootstrap, cold project macro build, cached expansion, and a
  changed decorated declaration
- peak RSS for each scalable frontend shape and cold LSP/native cases

The benchmark harness must emit phase timings and CSV. Add missing phase
instrumentation before guessing where time went.

### Initial Budgets

These are engineering targets, not language guarantees:

| Operation | Target |
| --- | ---: |
| Cold LSP workspace usable | under 1 s |
| Warm hover or definition | under 50 ms |
| Warm references or semantic tokens | under 100 ms |
| Tiny no-op `dudu check` | under 100 ms |
| No-op `dudu build` | under 250 ms |
| One-module Dudu analyze/emit | under 200 ms |
| Cached native metadata load | under 100 ms |
| 1,000-type cold macro project | under 500 ms |
| Cached 1,000-type macro project | under 100 ms |

Hardware, toolchain, corpus, sample count, median, p95, and RSS must accompany
every published comparison.

### 2026-07-14 Baseline

Host: AMD Ryzen 9 9950X, 16 cores/32 threads, Ubuntu 24.04, GCC 13.3,
Clang 18.1.3. Compiler measurements use a Release Dudu build, five samples,
wall-clock medians/p95, and process peak RSS. Raw CSV lives under the ignored
`build/bench_compiler` directory; rerun commands are in `scripts/bench_compiler.sh`.

| Case | Median | p95 | Peak RSS |
| --- | ---: | ---: | ---: |
| tiny `duc check` | 20.9 ms | 22.2 ms | 54.4 MiB |
| cold small native scan | 47.4 ms | 50.9 ms | 78.1 MiB |
| cached small native metadata | 21.5 ms | 22.2 ms | 54.8 MiB |
| generated-CMake no-op build | 72.7 ms | 74.7 ms | 53.8 MiB |
| generated-CMake one-module edit | 331.8 ms | 340.7 ms | 109.8 MiB |
| Dudu-only module no-op emit | 21.5 ms | 23.0 ms | 57.9 MiB |
| Dudu-only one-module changed emit | 22.0 ms | 22.1 ms | 58.1 MiB |
| 50k indexing-heavy lines | 264.6 ms | 267.2 ms | 146.4 MiB |
| 50k native-heavy lines | 437.6 ms | 499.0 ms | 157.9 MiB |

The dogfood LSP probe now starts a fresh server for every sample and records
initialization through first published diagnostics, first native hover,
definition, references, completion, semantic tokens, rename, and process peak
RSS as CSV. Five-sample Release results are:

| Workspace and operation | Median | p95 | Peak RSS |
| --- | ---: | ---: | ---: |
| `raymarch-dd` workspace usable | 299.6 ms | 304.7 ms | 216.2 MiB |
| `raymarch-dd` first native hover | 13.5 ms | 14.5 ms | 216.2 MiB |
| `raymarch-dd` slowest warm request | 20.3 ms | 20.9 ms | 216.2 MiB |
| `dudu-webserver` workspace usable | 149.9 ms | 151.5 ms | 142.7 MiB |
| `dudu-webserver` first native hover | 6.2 ms | 6.7 ms | 142.7 MiB |
| `dudu-webserver` slowest warm request | 9.9 ms | 11.2 ms | 142.7 MiB |

The first published diagnostics define workspace usability. Document-symbol
timing after diagnostics is labeled post-diagnostics rather than cold, because
the diagnostic pass has already populated the project/native view.

The baseline also found 50k class-heavy lines taking 2,651.3 ms. Profiling
showed declaration and body checking copied the complete `Symbols` graph for
every class merely to add `Self` and generic names. A scoped reversible symbol
overlay removed those whole-program copies without adding a cache. Follow-up
three-sample medians are 84.8 ms at 50k generated lines, 155.3 ms at 100k, and
306.9 ms at 200k. The path is now approximately linear. The fast C++ suite and
the complete LSP smoke/recovery/synchronization/matrix suite remain green.

The changed-module build aggregate was also split at the Dudu/native boundary.
Both a no-op module emit and a guaranteed one-file Dudu edit complete in about
22 ms. The remaining roughly 310 ms in the aggregate changed build is CMake,
GCC compilation, and linking. Existing incremental fixtures assert that a
no-op does not load source modules and that an edit rewrites only the affected
generated artifacts.

The initial macro baseline put a cold 1,000-type project at 987.0 ms, with a
cached run at 50.6 ms. A content-addressed shared SDK precompiled header,
parallel SDK bootstrap, compact binary catalog literal, and separate parallel
compilation of package modules and worker glue reduced the five-sample cold
median to 410.1 ms. The measured cold path now contains 257.3 ms of external
C++ compilation, 30.9 ms of linking, and 16.3 ms of macro execution. Cached
projects take 52.5 ms. Both cold and cached paths meet the initial budgets; a
generic launcher/plugin ABI is not justified without a larger measured package
that fails them.

The generated C++ boundary milestone is complete. Application-native imports
are owned by generated module headers or sources instead of
`dudu_runtime.hpp`; every generated source includes its own header; runtime
support is selected by one structured feature scan; and generated-CMake builds
precompile the stable runtime header. On the 1,000-unit comparison workload the
external GCC phase fell from 487.7 ms and 156.6 MiB to 256.4 ms and 90.7 MiB.
Clean builds of all four dogfood projects pass. Details and boundary fixtures
are recorded in [Generated C++ Boundary Plan](generated-cpp-boundary-plan.md).

### Work Order

1. Fix cold LSP indexing. The server must build indexes in the background,
   share `ProjectIndex` and native metadata across requests, and remain useful
   while indexing. A first request may not block for ten seconds.
2. Profile native-header cold scan and cached metadata load. Remove redundant
   Clang invocations, deserialization, dependency validation, and graph merges.
3. Profile no-op and one-file generated-CMake builds. Avoid invoking or
   configuring native tools when source stamps prove nothing changed; when a
   native tool must run, preserve its normal output.
4. Continue measuring native backend fixed cost after the completed generated
   C++ boundary and feature-runtime work. Preserve the stable runtime PCH,
   public/private dependency fixtures, and separate cold, changed-module,
   native-header-edit, and no-op measurements.
5. Reduce macro package compilation. Build the macro SDK during toolchain
   installation where toolchain compatibility permits, parallelize independent
   bootstrap units, and profile generated package/launcher C++ separately.
6. Profile compiler startup and allocation only after the end-to-end cases
   identify frontend cost as material. Continue the compact/arena AST work only
   with broad benchmark proof.

### Completion Gate

- the table above has current measured results
- no tracked case exceeds its budget without a documented external native-tool
  cost and a concrete follow-up
- cold and warm LSP behavior pass deterministic JSON-RPC latency fixtures
- no-op and changed-module builds prove correct invalidation and unchanged
  generated-file mtimes
- macro comparison includes Dudu, Rust, C#, Swift, and Nim with honest caveats
- `raymarch-dd` and `dudu-webserver` remain correct and measurably responsive

## 2. Native C++ Compatibility

Continue [Native Header Hardening](le_plan.md#9-native-header-hardening) and the
[Native Compatibility Matrix](native-compatibility-matrix.md).

The main remaining correctness risk is template-heavy, overload-heavy C++:

- dependent return and associated types
- class/function partial and explicit specializations
- constrained overloads, defaulted arguments, and variadic packs
- reference, cv, pointer, callable, and move-only behavior
- inherited constructors and methods
- macros that expose normal callable/value declarations after preprocessing
- diagnostics when scanner metadata is incomplete or ambiguous
- cold/warm native cache parity across GCC/libstdc++ and Clang/libc++ spellings

Failures from ordinary C/C++ use should produce generic compiler fixes, not
library-specific branches. Wrapper headers are acceptable only for C/C++
facilities that also require wrappers in ordinary C++ practice, such as
partial-syntax or declaration-generating preprocessor macros.

Completion is a compatibility-matrix improvement, not a claim that all C++ is
finished. Every fixed class of failure needs a neutral fixture plus at least one
real library consumer.

## 3. Compiler Architecture And Readability

Continue the cleanup requirements in
[Le Plan: Compiler Readability](le_plan.md#18-compiler-readability-and-style-pass)
without starting an unbounded rewrite.

- keep parser, module resolution, binding, type checking, lowering, emission,
  diagnostics, LSP, and native boundaries separately owned
- remove one-line forwarding wrappers and always-constant generic helpers that
  do not express a boundary
- remove duplicate logic and unreleased compatibility paths
- keep semantic decisions on AST, `TypeRef`, symbol identity, and typed facts
- keep source/native strings only for diagnostics, trivia, explicit `cpp(...)`,
  and C/C++ boundary spelling
- split code files that materially exceed the project's size guideline
- investigate an arena/node-id or variant-shaped AST only through a written
  design and measured prototype; do not add a compatibility wrapper around the
  current recursive AST

The first cleanup milestone separated the former 752-line LSP references file
along actual ownership boundaries. Reference collection, rename edits, and the
shared project/symbol/native-identity query policy now live in separate modules
of 275, 296, and 220 lines. Native call identity matching remains structured on
`AstSelection`, `TypeRef`, and native symbol identity; the split does not copy
policy or introduce source-text lookup. The focused frontend, references,
rename, navigation, and text-synchronization tests remain green.

Run this work opportunistically alongside latency and native fixes. Do not stop
all product work for a cosmetic repository rewrite.

## 4. Remaining Language Decisions

There is no known missing feature that blocks the current Linux alpha. The
following are real language decisions, but they require concrete pressure
before implementation.

### External Conformance

[Protocols And Serde Design Notes](protocols-serde-design-notes.md) records the
open problem: owned Dudu types can implement abstract contracts through normal
inheritance, but third-party types cannot yet satisfy a static protocol without
being modified or wrapped.

Drive this with a real `serdd` package. First prove derive-based serialization
for owned classes using the existing macro system. Add external conformance only
when adapting a real imported type demonstrates the required lookup, coherence,
or generic-constraint rules. Do not add a general trait system merely because
C++ and Rust have one.

### Generic Constraints

Current generics are checked at concrete instantiation and support type/value
parameters and const arithmetic. Concepts, traits, and `where` clauses remain
absent intentionally. Add a constraint surface only when diagnostics or library
APIs cannot be made clear through existing abstract classes and instantiation
checking.

### Inheritance Edge Cases

The common model is implemented. Constructors spanning multiple storage-bearing
bases remain rejected. Keep that restriction unless a real C++ interop case
requires a coherent Python-shaped syntax and deterministic C++ lowering.

### Deliberate Non-Features

Expression macros, custom grammar, quote/splice syntax, core `async`/`await`,
comprehensions, lambdas, generators, properties, and dynamic runtime mutation
are not missing implementation tasks. They are deliberate exclusions unless
the language specification is explicitly changed.

## 5. Numeric And Data Ecosystem

The compiler now provides the syntax and type-system facilities needed by an
array/tensor library: arbitrary-rank indexing items, slices, ellipsis, new axes,
variadic index hooks, assignment hooks, shaped metadata, `dyn`, and const shape
arithmetic. Continue [Tensor Backend And Numeric Stack](tensor-backend-plan.md)
as library work.

- bottle the reference surface into normal `ndad` and `mald` packages
- keep runtime shapes the default ergonomic path
- retain optional static shapes for API, layout, GPU, and proof boundaries
- validate BLAS/OpenBLAS and AMD-friendly GPU backends through optional probes
- prove view ownership, copy/move boundaries, broadcasting, gather/scatter,
  reductions, autograd, and backend transfer without compiler package-name
  knowledge
- keep heavyweight GPU/native validation outside the default compiler loop

Do not put BLAS, autograd, tensor storage, broadcasting policy, or GPU dispatch
inside the compiler.

## 6. Editor And Diagnostics Polish

After cold-start architecture is fixed, continue
[Editor Intelligence](editor-intelligence-plan.md) and
[Language Server Plan](language-server-plan.md):

- richer recovery for malformed declarations and nested expressions
- symbol-identity-based references and rename in every supported scope
- native docs and definitions when Clang metadata provides them
- field, alias, constant, and generated-macro documentation
- accurate signature help and inlay hints for generic/native/index operations
- quick fixes tied to AST/token ranges
- formatter stability and idempotence across the complete public syntax

Editor correctness is part of language usability. It must use the compiler's
AST and indexes rather than independent regex or source-string semantics.

## 7. Distribution And Feedback

Continue [Distribution Plan](distribution-plan.md) independently from normal
compiler development:

- keep Linux release creation local and tag/manual driven
- validate Apple Silicon on a maintained Mac before advertising support
- keep Homebrew source bootstrap usable without pretending it is a verified
  native binary release
- maintain Marketplace and Open VSX extension publication
- keep AUR, `.deb`, source archives, checksums, update, rollback, and uninstall
  on the same immutable version
- collect real issue reports and promote reproducible failures into focused
  compiler/LSP/native fixtures

Do not make GitHub Actions part of the normal patch/test loop.

## Execution Order

1. P0 user-visible latency and current benchmark budgets from
   [Dudu Performance Tasks](performance-tasks.md)
2. Native template/header correctness exposed by real programs
3. Architecture/readability cleanup encountered during that work
4. Editor/diagnostic polish after cold indexing is fixed
5. `serdd`-driven external-conformance decision
6. Numeric libraries and optional native/GPU ecosystem proof
7. Distribution maintenance and platform validation in parallel

The first three items are the next compiler push. The others should not delay
measured latency and native-interop improvements.
