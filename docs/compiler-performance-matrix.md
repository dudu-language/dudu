# Compiler Performance Matrix

This matrix compares compiler latency on generated equivalent source programs.
It is separate from runtime benchmarks and macro benchmarks.

The benchmark reports two operations:

- **frontend**: parse and type-check without producing an executable where the
  toolchain exposes that operation
- **executable**: produce an unoptimized runnable program from clean output

Dudu self-contained executable production is reported as both `duc emit` and
the external C++ compile/link phase. This keeps the language frontend cost
visible instead of attributing GCC work to Dudu. The self-contained output
includes Dudu's runtime/prelude in one generated translation unit; normal
generated-CMake projects split reusable support and modules differently.

## Method

- one source file per language
- equivalent primitive fields, record/class declarations, methods, functions,
  calls, and a small entry point
- a 1,000-unit mixed workload
- fresh output artifact for each executable sample
- warm operating-system file cache; no package downloads
- median, p95, and peak RSS
- compiler versions and commands captured in the generated report

This is a compiler responsiveness comparison, not a language productivity or
generated-runtime comparison. Syntax and semantic work differ between
languages, and source line counts are not expected to match exactly.

## Reference Result

Measured July 15, 2026 on the reference machine documented in
[Performance](performance.md):

| Language | Operation | Median | p95 | Peak RSS |
| --- | --- | ---: | ---: | ---: |
| Dudu | frontend | 128.1 ms | 131.7 ms | 63.8 MiB |
| Dudu | C++ emission | 820.8 ms | 824.5 ms | 66.0 MiB |
| C++ backend for Dudu output | executable | 434.1 ms | 441.2 ms | 156.7 MiB |
| **Dudu self-contained toolchain** | **executable** | **1,258.5 ms** | **1,261.6 ms** | **156.7 MiB** |
| C++ | frontend | 21.4 ms | 21.7 ms | 27.5 MiB |
| C++ | executable | 240.0 ms | 244.6 ms | 67.3 MiB |
| Rust | frontend | 99.3 ms | 105.3 ms | 101.5 MiB |
| Rust | executable | 122.8 ms | 127.3 ms | 124.1 MiB |
| Swift | frontend | 322.2 ms | 326.6 ms | 115.5 MiB |
| Swift | executable | 1,050.8 ms | 1,063.8 ms | 195.7 MiB |
| Nim | frontend | 216.2 ms | 217.2 ms | 36.1 MiB |
| Nim | executable | 356.8 ms | 359.8 ms | 40.0 MiB |
| C# / MSBuild | executable | 791.9 ms | 1,358.5 ms | 149.6 MiB |
| Go | frontend | 153.3 ms | 157.0 ms | 210.9 MiB |
| Go | executable | 205.0 ms | 224.1 ms | 81.1 MiB |

The generated inputs are similar in bytes but not in lines because each
language expresses the workload differently:

| Language | Lines | Bytes |
| --- | ---: | ---: |
| Dudu | 10,003 | 159,749 |
| Generated Dudu C++ | 20,263 | 478,647 |
| C++ | 7,003 | 171,740 |
| Rust | 4,003 | 146,653 |
| Swift | 7,007 | 147,813 |
| Nim | 6,003 | 150,649 |
| C# | 9,006 | 221,656 |
| Go | 4,004 | 163,644 |

Toolchains:

- Dudu 0.1.0-alpha.13
- GCC 13.3
- Rust 1.94
- Swift 6.3.3
- Nim 1.6.14
- .NET 8.0.128
- Go 1.22.2

Reproduce the table with:

```sh
python3 scripts/bench_compiler_compare.py --samples 5
```

The harness writes raw samples, structured JSON, exact commands, source sizes,
and generated Markdown under `build/compiler_compare`.

## Current Finding

The Dudu frontend is competitive on this workload: it is 1.29 times the Rust
frontend time, 0.84 times Go, 0.59 times Nim, and 0.40 times Swift. GCC remains
substantially faster for its equivalent C++ input.

Dudu's complete self-contained path is not yet competitive. C++ emission takes
821 ms after the 128 ms frontend and dominates the result. The external GCC
backend then compiles Dudu's larger self-contained output in 434 ms. Emission
throughput is therefore a concrete compiler optimization target, separate from
parser and semantic-analysis performance.

## Interpreting Results

- Compare mixed frontend time to understand frontend scaling.
- Compare executable time to understand the default native production path.
- For Dudu, inspect `dudu emit` and `C++ backend` before using the combined total.
- The C# row includes MSBuild project orchestration because this installation
  does not expose a standalone `csc` command.
- Go reuses the installed standard-library build cache but receives changed
  package source for each executable sample.
- Do not compare this table with incremental project-driver builds; CMake, Cargo,
  SwiftPM, and MSBuild have different orchestration and cache behavior.
