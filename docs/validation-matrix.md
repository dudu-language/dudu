# Validation Matrix

This is the public-readiness validation map for Dudu. The goal is to keep the
default loop fast while still having clear commands for heavier native and
dogfood checks.

## Fast Default Checks

Run these before normal commits:

```sh
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./scripts/check_targets.sh
./scripts/check_ast_migration_guards.sh
./scripts/test_cli_and_project_smoke.sh
./scripts/test_dependencies.sh
```

These cover the compiler unit tests, positive/negative target fixtures, AST
migration guards, project driver smoke, formatting, C API smoke, dynamic library
smoke, `dudu new`, `dudu build`, `dudu run`, `dudu clean`, path dependencies,
Git dependencies, and lockfile stability.

## Local libc++ Portability

Run `./scripts/test-libcxx.sh` after changing filesystem, threading, native
scanner, or other standard-library-facing compiler code. It builds Dudu and the
full CTest suite with Clang plus libc++ in an isolated directory. This catches
Apple-adjacent C++ portability failures locally, but does not replace the final
Apple Silicon check for Xcode SDK headers, arm64 execution, or Homebrew.

## Editor Checks

Run these when changing LSP, semantic model, inlay hints, hover, references, or
navigation:

```sh
cmake --build build --target dudu_language_server_tests dudu_language_server_navigation_tests -j"$(nproc)"
./build/dudu_language_server_tests
./build/dudu_language_server_navigation_tests
./scripts/test_lsp_matrix.sh
./scripts/probe_lsp_optional.sh
```

Dogfood latency checks should stay opt-in because they depend on local repos:

```sh
./scripts/probe_lsp_dogfood_latency.py build-release/dudu-lsp \
    --samples 5 \
    --csv build/bench_compiler/lsp-dogfood.csv
```

## Native Library Probes

Run this when changing native scanner, native overloads, generated C++ emission,
pkg-config handling, CMake generation, or imported C/C++ type modeling:

```sh
./scripts/probe_optional.sh
```

The optional probe script skips missing SDKs clearly. It should never require a
fresh clone to install every native dependency before running the normal fast
suite.

## Public Examples

The public example set is:

- `dudu init hello && cd hello && dudu run`
- C/C++ native import smoke through fixtures and examples
- SDL/raylib examples when those packages are installed
- `examples/` through `./scripts/test_examples.sh`
- `/home/vega/Coding/LangDev/Dudu/dogfooding/raymarch-dd`
- `/home/vega/Coding/LangDev/Dudu/dogfooding/dudu-webserver`
- `/home/vega/Coding/LangDev/Dudu/dogfooding/dudu-datascience`

`scripts/test_examples.sh` checks that public examples use current language
syntax and validates optional native packages when present. Missing optional
packages are reported as skips, not failures. The examples should prefer
canonical native imports such as `from cpp import thread`,
`from c import SDL3/SDL.h`, and `from cpp.path import vendor/foo.hpp as foo`;
old `import cpp "..."` / fake `as std` forms should not reappear.
Architecture-specific examples remain syntax-checked on every platform but are
compiled only on compatible targets. In particular, `native_escape.dd` uses
x86 SSE intrinsics and the x86 `pause` instruction, so its object check is an
explicit skip on ARM64 rather than an invalid portability requirement.

Release lanes may set `DUDU_SKIP_OPTIONAL_NATIVE=1` when optional third-party
SDK coverage belongs to another host. The Apple Silicon lane uses this mode so
its result depends on Xcode, ARM64, libc++, and Dudu's declared prerequisites,
not on incidental Homebrew formulae in the hosted runner image. Required Dudu
examples remain checked and compiled there; the Linux lane retains the broad
pkg-config and native-library matrix.

Run local dogfood checks with:

```sh
./scripts/test_dogfood.sh
```

The dogfood script skips missing local repos. Those repos are not part of the
compiler test suite; they prove current usability against real projects.

## Numeric Stack Checks

Compiler-level tensor/indexing behavior lives in this repo under fixtures and
target manifests. Dogfood library behavior lives in `dudu-datascience`.

Minimum checks:

```sh
./scripts/check_targets.sh
cd /home/vega/Coding/LangDev/Dudu/dogfooding/dudu-datascience
dudu run --timings
./scripts/check_target_api.sh
```

Optional BLAS/OpenCL checks belong in `dudu-datascience` and should skip clearly
when the backend is unavailable.

## Release Candidate Checks

Before tagging a release, run:

```sh
./scripts/test_full.sh
./scripts/probe_optional.sh
./scripts/test_dogfood.sh
git diff --check
```

Release candidates should also be tested from a fresh clone with
`./scripts/install-local.sh` on Linux. macOS should use the same install script
once the local machine has Xcode command line tools and CMake installed.

The authoritative release entry point is now:

```sh
./scripts/release-check.sh
```

The manual Apple Silicon preflight runs the release-script assertions from
`master` before another immutable candidate tag is created. Release test
scripts must report the command and line that failed; bare assertion exits are
not actionable on a hosted platform.

In addition to compiler, LSP, native, dogfood, and clean-install checks, it
builds and clean-installs the production VSIX, validates Homebrew/AUR recipes,
builds and extracts the `.deb`, verifies package-manager self-update refusal,
and runs a two-version bootstrap install/update/rollback/uninstall lifecycle.
These distribution checks intentionally stay out of the normal fast loop.
