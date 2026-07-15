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
| Dudu | frontend | 136.1 ms | 136.4 ms | 64.0 MiB |
| Dudu | C++ emission | 230.1 ms | 240.9 ms | 66.2 MiB |
| C++ backend for Dudu output | executable | 487.7 ms | 488.9 ms | 156.6 MiB |
| **Dudu self-contained toolchain** | **executable** | **716.2 ms** | **729.0 ms** | **156.6 MiB** |
| C++ | frontend | 21.7 ms | 23.3 ms | 27.5 MiB |
| C++ | executable | 264.8 ms | 269.8 ms | 66.5 MiB |
| Rust | frontend | 112.5 ms | 118.1 ms | 101.6 MiB |
| Rust | executable | 141.1 ms | 142.1 ms | 124.2 MiB |
| Swift | frontend | 342.9 ms | 347.1 ms | 115.1 MiB |
| Swift | executable | 1,191.1 ms | 1,268.7 ms | 194.6 MiB |
| Nim | frontend | 233.9 ms | 237.4 ms | 35.6 MiB |
| Nim | executable | 383.5 ms | 392.6 ms | 39.9 MiB |
| C# / MSBuild | executable | 865.5 ms | 1,459.9 ms | 149.0 MiB |
| Go | frontend | 176.9 ms | 182.9 ms | 210.6 MiB |
| Go | executable | 231.1 ms | 251.2 ms | 81.1 MiB |

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
| Go | 4,004 | 163,663 |

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

The Dudu frontend is competitive on this workload: it is 1.21 times the Rust
frontend time, 0.77 times Go, 0.58 times Nim, and 0.40 times Swift. GCC remains
substantially faster for its equivalent C++ input.

Dudu's complete self-contained path remains slower than the direct native
toolchains. The external GCC backend takes 487.7 ms to compile Dudu's larger
self-contained output. The complete `duc emit` command, including frontend
analysis and C++ emission, is now 230.1 ms after replacing two quadratic
whole-program scans with indexed dependency and symbol lookup. The same command
previously took 820.8 ms.

The fixed emission curve is 0.06, 0.11, 0.23, and 0.45 seconds at 250, 500,
1,000, and 2,000 units respectively. It is approximately linear over this
range. Further work should reduce generated support volume and ordinary
type/expression lowering costs rather than masking another known quadratic
curve.

## Generated C++ Anatomy

The 1,000-unit Dudu fixture emits 20,263 lines and 478,647 bytes of
self-contained C++, compared with 7,003 lines and 171,740 bytes for the compact
handwritten C++ fixture. This is not a threefold expansion in program semantics.
The generated file contains:

| Section | Lines | Bytes |
| --- | ---: | ---: |
| Runtime, standard includes, and build prelude | 253 | 10,342 |
| Class forward declarations | 1,000 | 15,890 |
| Free-function declarations | 1,002 | 31,797 |
| Class declarations | 6,000 | 75,890 |
| Out-of-line method definitions | 7,000 | 191,779 |
| Free-function definitions | 5,000 | 152,669 |
| `main` | 8 | 280 |

Of the whole file, 2,002 source-location comments occupy 185,962 bytes and
4,005 lines are blank. Removing comments and blank lines leaves 14,256 lines
and 288,680 bytes. The remaining difference primarily comes from conservative
forward declarations, function prototypes, and out-of-line method bodies. That
structure preserves arbitrary declaration order and module boundaries; it is
not runtime abstraction.

Five repeated local GCC builds isolated the compile-time cost:

| Input | Median wall time | Peak RSS |
| --- | ---: | ---: |
| Full generated Dudu C++ | 430 ms | 160 MiB |
| Generated body with only `<cstdint>` | 210 ms | 74 MiB |
| Handwritten C++ | 230 ms | 69 MiB |
| Handwritten C++ plus Dudu's full prelude | 460 ms | 158 MiB |
| Generated body with a prepared Dudu precompiled header | 250 ms | 133 MiB |

The verbose generated body is therefore not the cause of the current GCC
slowdown: without the prelude, it compiles as fast as the handwritten fixture.
The fixed cost comes from unconditionally parsing 22 standard headers and all
Result, tuple, indexing, array-view, hosted-I/O, and shader support for a program
that only uses `i32`. The source comments inflate inspectable output size but
have negligible compile-time cost. Unused runtime templates also do not produce
corresponding executable code; the unoptimized binaries in this experiment were
241 KiB for Dudu and 234 KiB for handwritten C++.

The concrete backend work is:

1. Make self-contained emission include only the standard headers and runtime
   components required by the analyzed program.
2. Build the shared `dudu_runtime.hpp` once as a project precompiled header in
   generated-CMake builds so every module does not parse the same standard
   library surface again.
3. Keep source mapping available, but shorten paths or make inspectable comments
   configurable independently of semantic emission.
4. Reduce declarations only where dependency analysis proves they are
   unnecessary; do not trade correct recursion, arbitrary declaration order, or
   separate-module compilation for a smaller generated file.

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
