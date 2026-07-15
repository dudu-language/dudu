# Performance

Dudu has four separate performance surfaces:

1. generated program runtime
2. Dudu frontend and project build latency
3. editor latency
4. compile-time macro latency

One number cannot represent all four. The tables below are measurements, not
claims that every Dudu program or every competing compiler has the same shape.

## Reference Machine

Unless a table says otherwise, the current reference run uses:

- AMD Ryzen 9 9950X, 16 cores / 32 threads
- Ubuntu 24.04
- GCC 13.3 and Clang 18.1.3
- Dudu 0.1.0-alpha.13, Release build
- five samples for published comparisons
- median and p95 wall time

Raw compiler and LSP results are written under the ignored
`build/bench_compiler` directory. Runtime results are written under the ignored
`build/benchmarks` directory.

## Generated Runtime

Dudu compiles to C++20. The runtime acceptance criterion is parity with
equivalent handwritten C++ using the same data structures, operations, C++
compiler, and optimization flags.

The current suite uses GCC 13.3 with `-O3 -DNDEBUG`, 10,000,000 input elements
or iterations, and five samples:

| Benchmark | Dudu median | C++ median | Dudu / C++ |
| --- | ---: | ---: | ---: |
| scalar arithmetic | 9.156 ms | 9.309 ms | 0.984 |
| pointer traversal | 9.230 ms | 9.485 ms | 0.973 |
| struct field access | 19.009 ms | 19.390 ms | 0.980 |
| fixed arrays | 20.240 ms | 19.260 ms | 1.051 |
| `list` / `std::vector` | 107.783 ms | 109.018 ms | 0.989 |
| tuple return | 20.134 ms | 20.372 ms | 0.988 |
| function callback | 39.832 ms | 39.146 ms | 1.018 |

The observed range is 0.97-1.05 times the handwritten C++ result. These small
differences are consistent with measurement and code-layout noise. This suite
does not show a systematic Dudu runtime tax for the covered operations.

Reproduce it with:

```sh
./scripts/bench.sh 10000000 --samples 5 \
    --emit-report build/benchmarks/runtime.json
```

## Frontend And Build Latency

| Operation | Median | p95 | Peak RSS |
| --- | ---: | ---: | ---: |
| tiny `duc check` | 20.9 ms | 22.2 ms | 54.4 MiB |
| cold small native scan | 47.4 ms | 50.9 ms | 78.1 MiB |
| cached native metadata | 21.5 ms | 22.2 ms | 54.8 MiB |
| generated-CMake no-op build | 72.7 ms | 74.7 ms | 53.8 MiB |
| generated-CMake one-module edit | 331.8 ms | 340.7 ms | 109.8 MiB |
| Dudu-only no-op module emit | 21.5 ms | 23.0 ms | 57.9 MiB |
| Dudu-only changed-module emit | 22.0 ms | 22.1 ms | 58.1 MiB |
| 50k indexing-heavy lines | 264.6 ms | 267.2 ms | 146.4 MiB |
| 50k native-heavy lines | 437.6 ms | 499.0 ms | 157.9 MiB |

The feature-dependent frontend range is roughly 114,000-650,000 generated
source lines per second. Generated line count is useful for Dudu regression
tracking, but it is not a fair direct comparison with another language because
equivalent programs require different source sizes and frontend work.

The class-heavy semantic path previously took 2,651 ms at 50,000 generated
lines because it copied the complete symbol graph for every class. Scoped symbol
overlays reduced that case to 84.8 ms. Follow-up measurements are 155.3 ms at
100,000 lines and 306.9 ms at 200,000 lines, which is approximately linear.

The 332 ms changed-project build includes about 22 ms of Dudu analysis and
emission. The remaining time belongs to CMake, GCC compilation, and linking.
Dudu reports these phases separately.

Reproduce the frontend suite with:

```sh
dudu bench compiler -- --suite core --samples 5
```

## Editor Latency

The LSP benchmark starts a fresh server for each sample. Workspace usability is
measured through the first published diagnostics. Warm requests run afterward
against the populated project and native indexes.

| Workspace and operation | Median | p95 | Peak RSS |
| --- | ---: | ---: | ---: |
| `raymarch-dd` workspace usable | 299.6 ms | 304.7 ms | 216.2 MiB |
| `raymarch-dd` first native hover | 13.5 ms | 14.5 ms | 216.2 MiB |
| `raymarch-dd` slowest warm request | 20.3 ms | 20.9 ms | 216.2 MiB |
| `dudu-webserver` workspace usable | 149.9 ms | 151.5 ms | 142.7 MiB |
| `dudu-webserver` first native hover | 6.2 ms | 6.7 ms | 142.7 MiB |
| `dudu-webserver` slowest warm request | 9.9 ms | 11.2 ms | 142.7 MiB |

The probe covers definition, hover, references, completion, semantic tokens,
rename, native-aware hover, and process RSS.

## Declaration Macro Compilation

The macro comparison generates one method for each of 1,000 two-field types.
Package downloads and the first Swift Syntax dependency bootstrap are excluded.

| Language | Clean app | Warm app | Unrelated edit | Decorated edit |
| --- | ---: | ---: | ---: | ---: |
| Rust 1.94 | 153.6 ms | 37.3 ms | 88.3 ms | 88.8 ms |
| Dudu, current | 519.2 ms | 54.2 ms | 51.6 ms | 63.6 ms |
| Nim 1.6 | 819.7 ms | 176.2 ms | 192.6 ms | 194.5 ms |
| Swift 6.3 | 2,160.3 ms | 454.7 ms | 1,404.4 ms | 1,352.9 ms |
| C# / .NET 8 | 2,440.3 ms | 502.8 ms | 1,565.9 ms | 1,592.9 ms |

The current Dudu row uses three samples after the latest optimization; the
other rows use five. It is therefore shown as a current result, not silently
substituted into the original five-sample table. About 400 ms of the Dudu cold
case is external GCC compile/link work. Macro execution itself is about 2 ms.

See [Macro Performance Matrix](macro-performance-matrix.md) for workload details,
package compile time, RSS, historical results, and reproduction commands.

## Cross-Language Compiler Matrix

The macro table is not a general compiler comparison. A separate compiler
matrix uses generated equivalent programs and reports:

- mixed declaration/function frontend checking
- complete unoptimized executable production
- Dudu frontend and external C++ backend time separately
- median, p95, and peak RSS

See [Compiler Performance Matrix](compiler-performance-matrix.md). The harness is
`scripts/bench_compiler_compare.py`.

On the current 1,000-unit workload, Dudu's frontend takes 127.7 ms, compared
with Rust at 112.5 ms, Go at 176.9 ms, Nim at 233.9 ms, and Swift at 342.9 ms.
Dudu C++ emission takes 219.4 ms and the external GCC backend takes 256.4 ms,
for a 477.4 ms self-contained executable path. The matrix keeps those phases
separate.

Emission previously took 820.8 ms because class dependency ordering compared
every field type with every class name twice, while member lowering compared
every member receiver with every class name. Direct dependency indexing and
semantic receiver classification reduced emission by 3.6 times. Measurements
from 250 through 2,000 units now scale approximately linearly.

Feature-sensitive runtime emission then removed unconditional parsing of 22
standard headers and unused Result, tuple, indexing, collection, hosted-I/O,
and shader support. That reduced the external C++ backend from 487.7 ms and
156.6 MiB to 256.4 ms and 90.7 MiB on the same workload.

## Current Assessment

Dudu-generated runtime performance is already in the intended C++ class for the
covered operations. Frontend checking, incremental project emission, and editor
latency are suitable for current dogfood projects. Self-contained executable
production is not yet as fast as direct native toolchains and remains measured
as separate Dudu-emission and C++-backend phases.
Macro builds are competitive when warm, while cold macro package compilation
still has a material external C++ compiler cost.

The alpha label remains appropriate because native compatibility, platform
validation, diagnostic coverage, language/ABI stability, and package ecosystem
are still developing. It is not an indication that normal generated programs
run at interpreter-like speed.
