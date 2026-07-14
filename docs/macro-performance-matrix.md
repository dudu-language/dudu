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

## Follow-Up Dudu Result

The next Dudu implementation combines generated package modules into one
translation unit and uses a shared SDK precompiled header. A three-sample run
of the same 1,000-type workload measured:

| Case | Median | p95 |
| --- | ---: | ---: |
| first SDK bootstrap plus app | 2723.1 ms | 2773.6 ms |
| macro package compile | 367.7 ms | 369.4 ms |
| macro package link | 32.3 ms | 32.7 ms |
| cold app after SDK bootstrap | 519.2 ms | 519.8 ms |
| warm app | 54.2 ms | 54.6 ms |
| unrelated edit | 51.6 ms | 52.8 ms |
| decorated-helper edit | 63.6 ms | 63.8 ms |
| handwritten app | 25.8 ms | 26.9 ms |

This result is not substituted into the five-sample cross-language table
because its sample count differs. It records a further 50% cold improvement
over the prior 1.04-second Dudu result. About 400 ms remains in the explicitly
measured external GCC compile/link boundary.

## Reproduction

Run the Dudu suite and every installed comparison adapter with:

```text
dudu bench compiler -- --suite macros --samples 5 --compare rust,csharp,swift,nim
```

The harness writes raw per-sample timing, RSS, and Dudu macro phase metrics
under `build/bench_compiler`. Comparison adapters live in
`scripts/bench_macro_compare_*.sh` and generate their fixtures from source so
the tested declaration count and edit classes stay explicit.
