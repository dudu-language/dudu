# Macro Performance Plan

User-defined macros are compile-time dependencies. A convenient macro system
that makes every edit slow is not acceptable.

This plan defines measurements and release budgets for the macro system in
[Dudu Macro System Plan](macro-syntax-plan.md). Correctness tests remain in the
fast test loop. Larger throughput and comparison benchmarks run explicitly
through `dudu bench compiler`.

## What Costs Time

Macro-heavy compilation has distinct costs that must be reported separately:

1. compiling or loading the macro package for the host
2. starting and negotiating with the worker
3. serializing the public input AST
4. executing the macro
5. validating and deserializing its expansion
6. merging generated declarations
7. resolving and type-checking generated code
8. lowering and emitting generated code

Only items 3 through 6 are macro expansion time. A benchmark that reports one
combined number cannot identify whether the macro engine, generated code, or
the native C++ backend is responsible for a regression.

Rust procedural macros illustrate the distinction: rustc compiles third-party
macro crates, converts between its internal token representation and the stable
`proc_macro::TokenStream`, executes through a proc-macro server, synthesizes the
result into its AST, and then compiles the generated program. Dudu should avoid
the token conversion and per-invocation startup costs, but generated code still
has a real downstream compilation cost.

References:

- <https://rustc-dev-guide.rust-lang.org/macro-expansion.html>
- <https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html>

## Measurement Rules

- Build `duc`, `dudu`, and macro packages in Release mode.
- Record CPU, OS, compiler versions, Dudu revision, and benchmark fixture hash.
- Pin worker count and native compiler flags in the report.
- Run at least one warm-up and five measured samples.
- Report median, p95, peak RSS, input declarations, input fields, generated
  nodes, output lines, and cache hit rates.
- Keep cold, warm, cached, and incrementally invalidated runs separate.
- Compare macro output against an equivalent checked-in handwritten Dudu
  fixture, not merely against an empty program.
- Store machine-readable CSV or JSON under `build/bench_compiler`; do not commit
  machine-specific benchmark results as language promises.

## Required Fixtures

### No-Op Derive

A derive inspects a declaration and emits nothing. This isolates protocol,
dispatch, serialization, and merge overhead.

Scales:

- 1 declaration
- 100 declarations
- 1,000 declarations
- 10,000 declarations

### Debug Derive

Generate one method that reads every field. Run 10, 50, and 200-field classes,
then projects containing 100 and 1,000 ordinary classes.

### Json Derive

Generate encode and decode functions for primitives, nested classes, enums,
lists, dictionaries, optional values, renamed fields, and skipped fields.

### Expansion Volume

Generate fixed amounts of ordinary Dudu AST: 1,000, 10,000, 100,000, and
1,000,000 nodes. This measures builder, protocol, validation, source mapping,
and merge throughput independently of macro author logic.

### Incremental Edits

Measure:

- an unrelated source edit
- a function-body edit outside a decorated declaration
- one field added to one decorated class
- one helper attribute changed
- one macro helper module changed
- one declared external input changed

Record macro binary rebuilds, worker restarts, expansion invocations, cache
hits, invalidated modules, and total frontend time.

### Failure Paths

Measure malformed output, an explicit macro diagnostic, worker crash, timeout,
oversized output, and expansion conflict. Failures must remain bounded and must
not poison later requests in the compiler or LSP session.

### Editor Workload

Open a macro-heavy project, then measure diagnostics, hover, completion,
definition, references, semantic tokens, and one-character edits. The LSP and
CLI must share macro binaries and expansion-cache logic rather than maintaining
independent expansion engines.

## Release Budgets

These are regression gates for the reference benchmark machine. Relative
budgets take precedence when absolute timings vary across hardware.

| Workload | Budget |
| --- | --- |
| Unrelated warm edit | zero macro executions; macro bookkeeping at most 3% of frontend time or 2 ms, whichever is larger |
| One decorated declaration changed | only that declaration and semantic dependents are invalidated |
| Cached worker acquisition | at most 20 ms p95 per macro package per compiler/LSP session |
| Simple derive execution | at most 5 ms p95 for one declaration after worker acquisition |
| 200-field derive execution | at most 20 ms p95 after worker acquisition |
| 1,000 no-op derives | at most 100 ms total warm expansion time |
| 1,000 ordinary Debug derives | at most 250 ms total warm expansion time |
| 1,000 cached derives versus handwritten | fixed frontend overhead at most 50 ms |
| 10,000 cached derives versus handwritten | total Dudu frontend time at most 1.20x the checked-in expanded fixture |
| Expansion transport | at least 250,000 AST nodes per second for payloads above 10,000 nodes |
| Idle worker memory | at most 64 MiB RSS above its macro package and shared runtime mappings |
| Crash or timeout recovery | diagnostic and clean worker replacement within 250 ms after termination is observed |

Cold macro-package compilation is reported but is not mixed into warm expansion
budgets. Its relative gate is no more than 1.25x the time required to compile an
equivalent normal host Dudu module with the same imports. Large SDK header costs
must be addressed with a compact runtime interface or precompiled artifacts,
not accepted as permanent macro overhead.

The separate 1,000- and 10,000-declaration gates distinguish bounded setup,
cache, and expansion-metadata costs from scaling behavior. A ratio at the
smaller scale is unstable because the handwritten frontend baseline is under
100 ms on the reference machine; the absolute gate remains strict there while
the larger fixture enforces the relative budget.

If a budget proves unrealistic on measured implementations, change it only
with benchmark evidence and an explanation of the architecture responsible.

## Cross-Language Comparisons

Cross-language results are informational because macro APIs, generated code,
compiler backends, and caching models differ. The comparison harness should
nevertheless include equivalent derive/source-generator workloads when the
toolchains are installed:

- Rust procedural derive
- C# incremental source generator
- Swift attached macro
- Nim typed AST macro

For each language, record:

- clean macro package build
- clean application build
- warm unchanged build
- unrelated edit
- one decorated declaration edit
- 100 and 1,000 generated declarations
- peak memory and generated output size

The Dudu release gate remains the handwritten-equivalent comparison. Beating a
different compiler on a synthetic benchmark does not excuse avoidable overhead
inside Dudu.

The current reference measurements and exact fixture caveats are recorded in
[Macro Performance Matrix](macro-performance-matrix.md). The comparison is
dated evidence, not a permanent language claim.

## Macro SDK Cache

Macro workers use two cache layers:

1. A global, content-addressed SDK cache stores optimized object files for the
   generated macro AST and protocol bridge. Its identity includes the C++
   compiler, standard, toolchain identity, generated SDK contents, bridge
   contents, include directories, defines, and compiler flags.
2. Each project stores content-addressed worker binaries and expansion results
   under its normal build cache. Worker launch code and package sources compile
   as separate translation units and independent units compile concurrently.

The launcher is compiled with `-O1`; macro package code and shared SDK objects
use `-O2`. This keeps worker startup and protocol code fast without making each
project repeatedly optimize generated SDK implementation code.

The SDK cache defaults to `$XDG_CACHE_HOME/dudu/macro-sdk`, then
`$HOME/.cache/dudu/macro-sdk`. `DUDU_MACRO_SDK_CACHE` overrides it. Publishing
uses an identity-named staging directory and atomic rename so concurrent Dudu
processes cannot expose partial entries.

The first macro use after installing a new Dudu/C++ toolchain must bootstrap
one SDK cache entry. That one-time operation is reported separately from a cold
project package build. Deleting a project's build directory must not force the
shared SDK to rebuild.

## Tooling

Extend the existing compiler benchmark harness with:

```text
dudu bench compiler -- --suite macros
dudu bench compiler -- --suite macros --samples 10
dudu bench compiler -- --suite macros --compare rust,csharp,swift,nim
```

Timing output must include:

```text
macro.package_build
macro.worker_start
macro.protocol
macro.execute
macro.validate
macro.merge
macro.generated_sema
macro.generated_codegen
```

The benchmark stays outside `scripts/test_fast.sh`. Fast tests should use small
fixtures to verify cache invalidation counts, deterministic output, protocol
limits, and failure recovery without repeatedly running throughput scales.

## Performance Acceptance

- Every required fixture exists and validates generated behavior.
- Timing categories do not overlap or hide native backend time.
- Warm and incremental runs assert exact macro invocation counts.
- Cache keys are deterministic and content-based.
- Compiler and LSP use the same worker and cache implementation.
- Benchmark reports expose median, p95, RSS, throughput, and expansion size.
- The release budgets pass on the recorded reference machine.
- Cross-language comparisons are reproducible but never part of the fast loop.
