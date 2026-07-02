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
./scripts/probe_lsp_dogfood_latency.py
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
- `/home/vega/Coding/Graphics/raymarch-dd`
- `/home/vega/Coding/Web/dudu-webserver`
- `/home/vega/Coding/ML/dudu-datascience`

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
cd /home/vega/Coding/ML/dudu-datascience
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
