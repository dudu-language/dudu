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
| scalar arithmetic | 8.926 ms | 9.044 ms | 0.987 |
| pointer traversal | 9.539 ms | 9.293 ms | 1.026 |
| struct field access | 19.510 ms | 19.472 ms | 1.002 |
| fixed arrays | 19.349 ms | 19.501 ms | 0.992 |
| `list` / `std::vector` | 109.968 ms | 110.810 ms | 0.992 |
| tuple return | 20.197 ms | 21.451 ms | 0.942 |
| function callback | 41.307 ms | 43.103 ms | 0.958 |
| particle update and mutable iteration | 38.836 ms | 39.369 ms | 0.986 |
| eight-thread accumulation | 1.568 ms | 1.560 ms | 1.005 |

The observed range is 0.94-1.03 times the handwritten C++ result. These small
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
| cold small native scan | 33.6 ms | 35.3 ms | 81.3 MiB |
| cached small native check | 8.7 ms | 9.3 ms | 57.1 MiB |
| project no-op check | 20.7 ms | 22.4 ms | 57.3 MiB |
| generated-CMake no-op build | 72.6 ms | 75.5 ms | 56.3 MiB |
| generated-CMake no-op run | 72.6 ms | 75.3 ms | 56.1 MiB |
| generated-CMake no-op test | 72.4 ms | 75.2 ms | 56.0 MiB |
| generated-CMake private dependency edit | 131.0 ms | 134.1 ms | 57.7 MiB |
| generated-CMake public interface edit | 159.0 ms | 165.1 ms | 57.8 MiB |
| generated-CMake native-header edit | 130.4 ms | 134.0 ms | 56.1 MiB |
| generated-CMake build-config edit | 422.1 ms | 440.7 ms | 88.6 MiB |
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

The current broad Release matrix adds inheritance/abstract dispatch and covers
functions, classes, expressions, wide module graphs, calls, control flow,
arrays, multidimensional indexing, type generics, payload matches, operators,
native declarations, and mixed projects. Three-sample representative medians:

| Shape | About 10k lines | About 50k lines | About 100k lines |
| --- | ---: | ---: | ---: |
| inheritance / abstract dispatch | 33.8 ms | 94.4 ms | 180.4 ms |
| expression-heavy | 71.6 ms | 305.0 ms | 589.6 ms |
| shaped indexing and swizzles | 63.5 ms | 258.5 ms | 502.9 ms |
| overloaded operators | 54.9 ms | 196.8 ms | 375.5 ms |
| native declarations and overloads | 102.0 ms | 436.3 ms | 870.3 ms |
| mixed nine-module project | 44.0 ms | 144.3 ms | 289.3 ms |

Actual generated line counts vary by shape and are recorded in the CSV. The
50k-to-100k representative curves are approximately linear; no whole-symbol or
whole-module copy surfaced. Million-line and malformed-edit sweeps remain
explicit P1 experiments rather than part of routine validation.

Clean per-module C++ emission uses the same generated projects. It removes the
output directory before every sample so incremental no-op behavior cannot hide
emission work:

| Shape | About 10k lines | About 50k lines | About 100k lines | 100k generated C++ |
| --- | ---: | ---: | ---: | ---: |
| inheritance / abstract dispatch | 54.1 ms | 230.3 ms | 389.7 ms | 150,562 lines / 4.9 MB |
| expression-heavy | 102.6 ms | 474.1 ms | 940.1 ms | 200,028 lines / 19.8 MB |
| shaped indexing and swizzles | 91.0 ms | 425.2 ms | 838.5 ms | 100,489 lines / 12.4 MB |
| overloaded operators | 74.0 ms | 332.6 ms | 638.7 ms | 104,501 lines / 7.3 MB |
| native declarations and overloads | 139.6 ms | 689.8 ms | 1,490.6 ms | 160,297 lines / 12.5 MB |
| mixed nine-module project | 55.8 ms | 188.4 ms | 378.4 ms | 123,527 lines / 8.0 MB |

Emission time, generated lines, and generated bytes remain approximately
linear for these bounded workloads. The expression fixture is intentionally
verbose and reaches about 1 GiB peak RSS at 100k input lines, so larger runs
remain explicit rather than part of the routine loop.

The project-driver matrix uses a two-module project with a local native header.
Private Dudu dependency edits rebuild only the changed generated module. Public
interface edits also rebuild dependents. Native-header edits are discovered by
CMake without forcing Dudu analysis. Build-config edits intentionally
regenerate CMake, configure, compile, and link.

A generated-CMake no-op still invokes `cmake --build`. That call is the native
dependency check: it is what discovers edits to user C/C++ sources and headers.
Dudu does not try to duplicate CMake's native dependency graph. On the no-op
path Dudu preserves generated mtimes, skips configure, reports `dirty 0
modules` and `analyze 0 modules`, and CMake skips compile and link. `--timings`
reports `cmake.generate cached`, `cmake.configure cached`, and `cmake.build
dependency-check` so the decision is observable.

Native header awareness has a separate explicit include-graph matrix:

| Header graph | Cold check | Cached check |
| --- | ---: | ---: |
| small C fixture | 33.6 ms | 8.7 ms |
| `string` + `unordered_map` + `vector` | 1,035.0 ms | 48.8 ms |
| 13-header STL interop fixture | 1,874.5 ms | 114.8 ms |
| SDL3 | 190.0 ms | 36.1 ms |
| raylib | 68.8 ms | 13.3 ms |

The larger cached check includes ordinary Dudu parsing and semantic analysis;
loading its cached native metadata itself takes about 18 ms. Direct imports are
scanned as one translation unit, with isolated fallback only when headers have
conflicting global declarations. `--timings` reports libclang, AST dump, AST
parse, macro preprocessing, cache, and serialization phases. The explicit
benchmark also probes both libstdc++ and libc++ when installed:

```sh
./scripts/bench_native_headers.py --samples 5
```

Reproduce the bounded frontend and clean-emission matrix from a checkout with:

```sh
./scripts/bench_compiler.sh --build-type Release --samples 3 \
    --line-scales 10000,50000,100000 \
    --shapes inheritance,expressions,indexing,operators,native,mixed \
    --emit-scaling \
    --csv build/bench_compiler/emission_scaling_release.csv
```

## Editor Latency

The LSP benchmark starts a fresh Release server for every sample. It measures
initial parser diagnostics, complete semantic diagnostics after a valid edit,
malformed-edit parser diagnostics, repair without restart, definition, hover,
references, rename, completion, signature help, semantic tokens, inlay hints,
formatting, first native queries, and process peak RSS. Five samples use the
same Ryzen 9 9950X, Ubuntu 24.04, GCC 13.3, and Clang 18.1.3 described above.

The main table reports p95 latency. Native metadata was already present on disk
for these five-sample warm-cache measurements.

| Workload | Usable | Parser edit | Repair | Hover | Definition | References | Semantic tokens | Peak RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| small generics | 7.6 ms | 0.3 ms | 0.2 ms | 0.1 ms | 0.2 ms | 0.2 ms | 0.2 ms | 57.9 MiB |
| declaration macros | 10.8 ms | 3.0 ms | 3.1 ms | 0.4 ms | 3.0 ms | 0.5 ms | 0.2 ms | 73.6 MiB |
| template-heavy native | 8.4 ms | 0.5 ms | 0.4 ms | 8.0 ms | 5.8 ms | 81.9 ms | 2.7 ms | 147.7 MiB |
| `raymarch-dd` | 9.7 ms | 3.0 ms | 2.0 ms | 2.8 ms | 0.6 ms | 8.9 ms | 0.3 ms | 276.0 MiB |
| `dudu-webserver` | 8.8 ms | 1.5 ms | 1.1 ms | 0.8 ms | 0.4 ms | 7.5 ms | 0.3 ms | 190.1 MiB |

The slowest p95 values outside that table are 34.8 ms for inlay hints, 25.7 ms
for signature help, 20.9 ms for rename, 11.9 ms for completion, and 0.4 ms for
formatting. Complete semantic diagnostics after adding an unresolved name take
1.1 ms for the small fixture, 25.2 ms for the macro fixture, 53.4 ms for the
template-heavy fixture, 238.8 ms for `dudu-webserver`, and 410.2 ms for
`raymarch-dd`. Parser diagnostics remain immediate while that complete project
analysis runs in the background.

Removing the dedicated template fixture's native metadata cache makes the
first native definition query take 1.43 seconds and 197.2 MiB. Workspace
usability remains 7.0 ms, and the same process then answers warm definition,
hover, and references in 5.4, 6.2, and 75.7 ms. The cold cost is an explicit
Clang header scan; it does not block Dudu parsing or unrelated editor queries.

Reproduce the tracked matrix with:

```sh
./scripts/probe_lsp_dogfood_latency.py build-release/dudu-lsp \
    --samples 5 \
    --csv build/bench_compiler/editor-reliability-release.csv
```

Measure the empty-cache native case separately with:

```sh
rm -rf tests/fixtures/lsp_latency_native/build
./scripts/probe_lsp_dogfood_latency.py build-release/dudu-lsp \
    --samples 1 --case template-native \
    --csv build/bench_compiler/editor-reliability-cold-native.csv
```

## Declaration Macro Compilation

The macro comparison generates one method for each of 1,000 two-field types.
Package downloads and the first Swift Syntax dependency bootstrap are excluded.

| Language | Clean app | Warm app | Unrelated edit | Decorated edit |
| --- | ---: | ---: | ---: | ---: |
| Rust 1.94 | 153.6 ms | 37.3 ms | 88.3 ms | 88.8 ms |
| Dudu, current | 410.1 ms | 52.5 ms | 53.3 ms | 67.5 ms |
| Nim 1.6 | 819.7 ms | 176.2 ms | 192.6 ms | 194.5 ms |
| Swift 6.3 | 2,160.3 ms | 454.7 ms | 1,404.4 ms | 1,352.9 ms |
| C# / .NET 8 | 2,440.3 ms | 502.8 ms | 1,565.9 ms | 1,592.9 ms |

The current Dudu row uses five Release samples. About 288 ms of the cold case
is external GCC compile/link work. Macro execution itself is about 16 ms for
the 1,000 generated methods.

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
