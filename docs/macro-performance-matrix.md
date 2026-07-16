# Macro Performance Matrix

This is an informational comparison of equivalent declaration-generating
macro workloads. It is not a claim that the macro systems have identical
semantics or that one number represents general compiler performance.

## Reference Run

- Date: 2026-07-14
- CPU: AMD Ryzen 9 9950X, 32 logical CPUs
- Dudu: 0.1.0-alpha.13, GCC 13.3 backend
- Rust: 1.94.0 procedural derive
- C#: .NET SDK 8.0.128 incremental source generator
- Swift: 6.3.3 attached member macro
- Nim: 1.6.14 typed AST macro
- Samples: five measured Release builds per case

Each fixture defines 1,000 two-field types and generates one method per type.
The handwritten case checks in the same methods without macro expansion.
Package dependency downloads and Swift's initial `swift-syntax` framework
bootstrap are excluded. Clean means the fixture's package/application build
artifacts were removed while downloaded toolchain dependencies remained.

All values are median wall time in milliseconds:

| Language | Macro package | Clean app | Warm app | Unrelated edit | Decorated edit | Handwritten app |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Dudu, before SDK cache | 3956.8 | 4118.9 | 114.2 | 112.8 | 127.6 | 78.6 |
| Dudu, SDK cache | 871.3 | 1042.0 | 110.6 | 108.9 | 124.5 | 75.6 |
| Rust | 176.4 | 153.6 | 37.3 | 88.3 | 88.8 | 96.6 |
| C# | 1255.5 | 2440.3 | 502.8 | 1565.9 | 1592.9 | 1357.5 |
| Swift | 761.8 | 2160.3 | 454.7 | 1404.4 | 1352.9 | 1138.8 |
| Nim | 773.6 | 819.7 | 176.2 | 192.6 | 194.5 | 792.1 |

The optimized Dudu path reduced the 1,000-type cold application build by
3.95x and macro package compilation by 4.54x. Peak cold-build RSS fell from
about 299 MiB to 226 MiB. Macro execution itself remained about 2 ms, so the
improvement came from removing repeated native SDK compilation rather than
weakening expansion work.

The first-ever Dudu SDK cache bootstrap on this machine takes about 4.8
seconds. It occurs once per SDK/toolchain identity, not once per project. A
normal cold project build after bootstrap is the 1.04-second result above.

## Current Dudu Result

The current Dudu implementation uses a shared SDK precompiled header and
compiles generated package modules separately from the small catalog/dispatch
worker. Those independent translation units compile in parallel. A five-sample
run of the same 1,000-type workload measured:

| Case | Median | p95 |
| --- | ---: | ---: |
| first SDK bootstrap plus app | 2513.5 ms | 2542.1 ms |
| macro package compile | 257.3 ms | 277.4 ms |
| macro package link | 30.9 ms | 32.8 ms |
| cold app after SDK bootstrap | 410.1 ms | 432.2 ms |
| warm app | 52.5 ms | 54.4 ms |
| unrelated edit | 53.3 ms | 54.2 ms |
| decorated-helper edit | 67.5 ms | 67.8 ms |
| handwritten app | 27.2 ms | 27.4 ms |

This records a further 21% cold improvement over the prior 519.2 ms result.
Separating package implementation from worker glue reduced median external GCC
compilation from 364.0 ms to 257.3 ms without changing macro semantics or
introducing a plugin ABI.

## Reproduction

Run the Dudu suite and every installed comparison adapter with:

```text
dudu bench compiler -- --suite macros --samples 5 --compare rust,csharp,swift,nim
```

The harness writes raw per-sample timing, RSS, and Dudu macro phase metrics
under `build/bench_compiler`. Comparison adapters live in
`scripts/bench_macro_compare_*.sh` and generate their fixtures from source so
the tested declaration count and edit classes stay explicit.
