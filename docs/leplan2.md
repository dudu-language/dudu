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
| generated-CMake no-op build | 72.6 ms | 75.5 ms | 56.3 MiB |
| generated-CMake private dependency edit | 131.0 ms | 134.1 ms | 57.7 MiB |
| generated-CMake public interface edit | 159.0 ms | 165.1 ms | 57.8 MiB |
| generated-CMake native-header edit | 130.4 ms | 134.0 ms | 56.1 MiB |
| Dudu-only module no-op emit | 21.5 ms | 23.0 ms | 57.9 MiB |
| Dudu-only one-module changed emit | 22.0 ms | 22.1 ms | 58.1 MiB |
| 50k indexing-heavy lines | 264.6 ms | 267.2 ms | 146.4 MiB |
| 50k native-heavy lines | 437.6 ms | 499.0 ms | 157.9 MiB |

The dogfood LSP probe now starts a fresh server for every sample and records
initialization through immediate parser diagnostics, first native hover,
definition, references, completion, semantic tokens, rename, and process peak
RSS as CSV. Five-sample Release results are:

| Workspace and operation | Median | p95 | Peak RSS |
| --- | ---: | ---: | ---: |
| `raymarch-dd` workspace usable | 7.5 ms | 8.0 ms | 280.6 MiB |
| `raymarch-dd` cached first native hover | 219.5 ms | 222.1 ms | 280.6 MiB |
| `raymarch-dd` slowest warm request | 21.0 ms | 22.8 ms | 280.6 MiB |
| `dudu-webserver` workspace usable | 7.5 ms | 7.8 ms | 173.7 MiB |
| `dudu-webserver` cached first native hover | 91.6 ms | 92.3 ms | 173.7 MiB |
| `dudu-webserver` slowest warm request | 11.8 ms | 12.3 ms | 173.7 MiB |

The server publishes recovering parser diagnostics immediately, builds full
semantic/native diagnostics on one coalescing background worker, drops stale
revisions, and asks the client to refresh semantic tokens when native metadata
is ready. Removing both dogfood native caches still leaves first diagnostics at
7.6-8.2 ms. The deliberate first native request then exposes the real cold scan:
1.08 seconds for `dudu-webserver` and 3.65 seconds for SDL-heavy `raymarch-dd`.

The baseline also found 50k class-heavy lines taking 2,651.3 ms. Profiling
showed declaration and body checking copied the complete `Symbols` graph for
every class merely to add `Self` and generic names. A scoped reversible symbol
overlay removed those whole-program copies without adding a cache. Follow-up
three-sample medians are 84.8 ms at 50k generated lines, 155.3 ms at 100k, and
306.9 ms at 200k. The path is now approximately linear. The fast C++ suite and
the complete LSP smoke/recovery/synchronization/matrix suite remain green.

The project-driver matrix is now explicit. Five-sample Release medians are 20.7
ms for no-op `check`, 72.6 ms for no-op `build`, 72.6 ms for no-op `run`, and
72.4 ms for warm no-op `test`. Private imported-module, public-interface,
native-header, and build-config edits take 131.0, 159.0, 130.4, and 422.1 ms
respectively.
The retained no-op `cmake --build` call is the native dependency check; Dudu
skips module analysis, emission, configure, compile, and link when they are not
needed. `--timings` reports the cache and dependency-check decisions.

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

The bounded scaling matrix now covers fourteen frontend shapes, including
inheritance/abstract dispatch. Representative 100k-line-equivalent Release
times are 180.4 ms for inheritance, 589.6 ms for expressions, 502.9 ms for
indexing, 375.5 ms for operators, 870.3 ms for native-heavy code, and 289.3 ms
for a mixed module graph. Clean emission over six representative shapes is also
approximately linear through 100k lines, ranging from 378.4 ms for the mixed
graph to 1,490.6 ms for native-heavy declarations. Generated line and byte
growth is recorded beside the timing CSV. The runtime parity matrix has nine
paired Dudu/C++ programs; five-sample ratios range from 0.942 to 1.026.

### Work Order

1. Complete: background LSP indexing, immediate parser diagnostics,
   stale-result rejection, and semantic-token refresh are covered by fixtures.
2. Complete for the current gate: direct native imports use one ordered Clang
   translation unit with isolated conflict fallback and deterministic cached
   metadata.
3. Complete for the current gate: no-op and mutation-specific generated-CMake
   paths are measured, correctly invalidated, and observable under `--timings`.
4. Complete: generated native dependencies, stable runtime PCH ownership, and
   public/private include boundaries have dedicated fixtures.
5. Complete for the current budget: macro modules compile independently in
   parallel and cold/cached 1,000-type projects meet their targets.
6. Evidence gate only: startup is material for tiny programs, but broad
   frontend curves remain linear. Do not change the AST representation without
   a measured workload that justifies it.

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

### 2026-07-16 Compatibility Milestone

The neutral native compatibility fixture now executes under both
GCC/libstdc++ and Clang/libc++. It covers:

- explicit function specialization and constrained overload selection
- structural template ordering, including nested `Box[T]` patterns versus
  unconstrained `T`
- type and non-type template parameter metadata
- defaulted arguments and variadic template packs
- reference binding and overload ranking across lvalues, const references,
  rvalues, forwarding references, and ref-qualified methods
- deleted free functions and methods with candidate diagnostics
- callable objects through direct `operator()` calls and `std::invoke`
- move-only construction and transfer
- inherited methods from instantiated generic bases
- declarations produced by normal preprocessor expansion
- methods returning their owning native class, including chained calls

The corresponding real standard-library consumers cover `std.less`,
`std::chrono::steady_clock::now`, and
`std.filesystem.path.filename().string()`. The next compatibility milestone
also covers associated member templates, template-template and variadic pack
bindings, partial-specialization requirements, static constexpr metadata,
compiler type transforms, declarations referenced before their AST-dump
definition, and C++ index aliases used by `std.string.substr`. Scanner fixes
were made in generic template, static-method, operator-method,
injected-class-name, scope qualification, and native identity handling; none
branch on a library or header name. Native scan cache format version 46
invalidates metadata produced before these rules.

The canonical executable and negative fixture suites pass. All three dogfood
projects pass, including the webserver HTTP smoke, advanced indexing,
BLAS/OpenCL targets, and autograd training. Focused libc++ validation passes
for the native compatibility fixture, filesystem consumer, and member type
tests. The complete libc++ suite remains an explicit portability job because
its native scanner tests are intentionally heavy.

Remaining work in this phase is driven by concrete matrix failures: richer
diagnostics when constraints are unavailable in scanner metadata, additional
template-heavy real-library probes, and native metadata/LSP polish where a
real consumer exposes missing identity or documentation. Do not add speculative
surface syntax or library-specific compatibility code.

Completion is a compatibility-matrix improvement, not a claim that all C++ is
finished. Every fixed class of failure needs a neutral fixture plus at least one
real library consumer.

## 3. Compiler Architecture And Readability

Continue the cleanup requirements in
[Le Plan: Compiler Readability](le_plan.md#18-compiler-readability-and-style-pass)
and track the subsystem audit and measured baseline in
[Compiler Cleanup](compiler-cleanup.md), without starting an unbounded rewrite.

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

Semantic-token ownership is also explicit now. One wire-format unit owns the
legend, token record, modifiers, source-range insertion, sorting, and LSP delta
encoding for both semantic and invalid-source lexical highlighting.
Expression/body inference is separate from module declaration traversal, and
top-level functions and methods share one parameter/body collector.

Editor-local scope ownership is shared as well. Navigation and inlay hints now
use one path for inferred locals, tuple bindings, loop elements, generic
function symbols, and receiver substitution. Inlay hints no longer inspect
source lines to rediscover declaration syntax, and imported class previews use
an owning presentation-symbol context instead of query-local declaration
copies.

Completion and signature help now have separate capability owners. Their
shared token-based call-site query handles dotted and generic calls plus the
active argument index, so neither capability maintains a private source-text
parser.

JSON-RPC transport is separate from language-server session policy.
Content-Length framing, synchronized writes, response/error envelopes,
notifications, and refresh request IDs have one owner; document state,
diagnostics scheduling, and capability dispatch remain in the server session.
Native import-target navigation is separate from ordinary AST and symbol
definition dispatch as well. Compiler and pkg-config include discovery,
external process ownership, and header path resolution now live in one
native-header query unit. `language_server_definition.cpp` is 456 lines instead
of 617 and no longer acquires build-tool dependencies transitively.

The command audit also consolidated the input-to-source-root rule shared by
check, index, emit, and native-cache cleanup. Relative input handling now has
one frontend owner instead of two private copies.

Project source-stamp persistence and invalidation are now separate from graph
construction. Canonical paths, file stamps, cache serialization, and change
detection live behind a private project-index boundary, reducing
`project_index.cpp` from 566 to 418 lines without leaking filesystem
implementation into the public index API.

Core dependency direction is clean as well. Shared text and identifier
operations live in `core/text`; `src/dudu/core` has no upward parser, semantic,
or code-generation includes, and non-codegen subsystems no longer import the
code-generation umbrella for generic string helpers. Native tag normalization,
macro diagnostic source-location conversion, shaped-type rendering, and
ellipsis parsing each have one owner instead of parallel copies or wrappers.

Project configuration ownership is explicit too. Manifest syntax and value
parsing live in one parser unit, while target application, upward discovery,
and project-relative path policy remain separate. Root and named targets share
one native-build entry parser, and always-constant or renaming helpers are
gone.

Test build ownership is explicit as well. One CMake helper owns executable
creation, linking, the C++ language level, strict warnings, warning-as-error
policy, and CTest registration. This removed three duplicated target lists and
exposed an expansion-render test that had been built but never registered.
Target-specific fixture definitions remain local to their tests. The former
1,732-line native frontend driver is now six independent fixture groups for
scan basics, import identity, deduplication, cache behavior, template metadata,
and import projection. The LSP matrix is no longer a Python program hidden in a
1,532-line shell heredoc; protocol, fixture, request, core assertion, native
assertion, and code-action ownership now live in bounded Python modules behind
a five-line launcher. The former 1,280-line general language-server test
driver is now four independent executables for diagnostics/recovery,
completion/inlay behavior, native editor intelligence, and project-index
cache behavior. Lint fixtures moved to the diagnostics owner, and the former
1,337-line navigation driver is now bounded definition/hover,
symbol-reference, and module/native-reference executables. The default fast
AST driver is likewise split into semantic-token, parser-recovery,
statement-syntax, and declaration/query ownership. The default fast CTest
frontend driver is now syntax/fixture, diagnostics/formatting, and
semantics/codegen ownership, with manifest behavior consolidated into the
project-config suite. Module loading/import identity, project indexing, and
generated module artifacts also have independent test owners. Core type
compatibility, native templates, member substitution/lookup, expression shape,
and type/array shape now have bounded independent fixtures as well. Macro
capability policy is separate from expansion/editor integration, and emission
fixtures are separated into native/pointer, collection/match, and
class/function ownership. The default fast CTest inventory is 52
ownership-specific targets. The 533-line native template metadata fixture
remains one cohesive documented exception rather than being split across
shared generated-header vocabulary.

Running-executable and `PATH` lookup are now shared support operations rather
than separate CLI, standard-library, macro, and native-scanner platform
implementations. Each caller still owns its toolchain identity and layout
policy.

Macro worker ownership is explicit as well. Source and binary identities share
one canonical hashing path, worker and SDK artifact generation use shared
compiler-internal file I/O, and native compile commands use one bounded
parallel runner. Generated protocol conversion remains generated,
capability-scoped macro I/O remains isolated from compiler file access, and
atomic publication remains local to each cache policy.

The semantic and native API audit removes unreleased aliases rather than
preserving them. Expression presence, decorator string literals, receiver
class lookup, and native metadata merging use their canonical structured
operations directly.

The final core audit also removed the remaining duplicate trimming APIs,
one-use pointer/construction/import forwarding helpers, and the allocation
semantic trampoline. Generic AST traversal templates now live in
`core/ast_visit.hpp` instead of the AST data header, so consumers opt into
behavior explicitly. Remaining hand-written files slightly above the
300-500-line guideline were reviewed and retained only where they own one
stateful grammar, semantic, module-loading, native-matching, or LSP-session
operation; the rationale is recorded in
[Compiler Cleanup](compiler-cleanup.md#final-size-and-wrapper-audit).

The code-generation audit also removed index-operator lowering from the general
call emitter. Index reads, writes, and compound writes now share one structured
receiver/type-resolution path in an owned index-hook unit. Duplicate and stale
declarations were removed, and shared enum payload classification moved to
semantic enum support instead of remaining a call-emission helper.

The parser audit separated source/token-piece reconstruction and handoff to the
expression and type token parsers from top-level declaration grammar and
recovery. `parser.cpp` now owns the grammar cursor and recovery policy; the
syntax-piece unit owns only token ranges, diagnostic spelling, and structured
subparser entry.

The expression-emission audit separated template-call lowering from the general
expression dispatcher. Allocation, pointer casts, layout builtins,
shape-assumption calls, generic constructors, and native C++ template calls now
use one structured `Expr`/`TypeRef` path. Wrapper-type classification also moved
from parser utilities into the core type model, removing a parser-to-codegen
ownership leak. `cpp_expr_emit.cpp` is now 445 lines and the owned template-call
unit is 203 lines.

The module-emission audit separated generated-name, native-alias,
imported-symbol, and public-ABI projection from artifact rendering and file
orchestration. Singleton and multi-module builds now use one span-based path,
so emission does not copy the root AST or maintain duplicate regular and test
branches. The unused module-write forwarding API was removed.

Class emission is separated by responsibility as well. Class ordering, class
shape, fields, constants, and artifact orchestration remain in the class
layout unit. Constructor and `super.init` handling, receiver substitution,
virtual/abstract policy, operator names, method signatures, bodies, and
out-of-line definitions have one class-method owner. Free functions, classes,
and enum methods also share one C++ template-parameter renderer and one
header-visibility predicate instead of maintaining three formatting copies.
Free-function decorators, signatures, variadic packs, generic/header
ownership, test filtering, bodies, and C ABI declarations now have one owner
as well. Class forward declarations moved into class emission, so
`cpp_emit.cpp` only assembles generated header, source, module, and test
artifacts. It is now 272 lines; the free-function unit is 286 lines, the class
layout unit is 265 lines, and the class-method unit is 373 lines.
Function/class decorator lookup and test-function classification are core
declaration facts now. Semantic analysis, code generation, module ABI
projection, and test discovery no longer maintain subsystem-prefixed wrappers
or independent definitions of what constitutes a test.
Generic method inference now has one recursive enum/class/base traversal for
both argument-only and expected-return binding. Function declaration checking
also has one shared rule for variadics, duplicate parameters, parameter types,
and return types across enum methods, class methods, and free functions.
`sema_methods.cpp` is 560 lines instead of 597 and
`sema_declarations.cpp` is 549 lines instead of 575.
Native partial-specialization matching and constexpr extraction now have their
own typed semantic owner instead of sharing a 602-line associated-type
resolver. Clang AST-dump ingestion is likewise split into type normalization,
scan support, declaration extraction, and a 296-line line orchestrator instead
of one 755-line parser. These are ownership splits only: native declarations,
specializations, identities, and generated C++ remain behaviorally unchanged.
LSP hover presentation now has one Markdown fence/serialization owner, native
identity path lookup lives at the native boundary, and primitive/native-alias
hover construction lives with type hover. The general hover dispatcher is 469
lines instead of 588 and no longer owns those independent semantic tables.
Direct hover and inlay label parts now also share class previews, native and
primitive type documentation, layouts, and definition locations.
`language_server_inlay_type_details.cpp` is 108 lines instead of 216.
Workspace ownership is explicit. The server records client `rootUri` and
`workspaceFolders`, adds manifest roots discovered from open documents, and
does not recursively scan the parent of a configless scratch file. A dedicated
fixture verifies client-root discovery, manifest sibling discovery, and
scratch isolation. This prevents unrelated duplicate modules from silently
changing code actions or navigation results.

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
   [Dudu Performance Tasks](performance-tasks.md) (complete for the current
   bounded gate)
2. Native template/header correctness exposed by real programs (next)
3. Architecture/readability cleanup encountered during that work
4. Editor/diagnostic polish after cold indexing is fixed
5. `serdd`-driven external-conformance decision
6. Numeric libraries and optional native/GPU ecosystem proof
7. Distribution maintenance and platform validation in parallel

The first three items are the next compiler push. The others should not delay
measured latency and native-interop improvements.
