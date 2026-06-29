# Repository Layout

This repository is the Dudu compiler, tooling, docs, examples, and tests. It
should not contain personal scratch projects or generated playground build
output.

## Top Level

- `src/main.cpp`: tiny executable entry point shared by `dudu` and `duc`.
- `src/dudu/`: compiler, project driver, C++ emitter, LSP, formatter, native
  header scanner, and shared tooling library implementation.
- `include/dudu/`: public C/C++ headers exported by Dudu when needed.
- `tests/`: C++ unit tests and `.dd` fixtures for compiler and LSP behavior.
- `examples/`: curated Dudu examples that should stay reproducible.
- `benchmarks/`: generated or curated compiler benchmark inputs.
- `scripts/`: build, test, probe, install, and developer helper scripts.
- `editors/`: editor integrations, including the VS Code extension.
- `docs/`: language, compiler, project-driver, LSP, and planning documents.

The `src/main.cpp` plus `src/dudu/` split is intentional. `src/main.cpp` is only
the CLI front door; the actual compiler source lives under `src/dudu/` and is
built as the `dudu_frontend` library.

## What Does Not Belong Here

Do not put personal scratch repos or generated playground output inside this
repo. In particular, `duduplayground` is an external sibling dogfood/scratch
repo when it exists locally, not a subdirectory of this repository.

Dogfood projects such as these should live next to the Dudu repo:

- `/home/vega/Coding/GameDev/duduplayground`
- `/home/vega/Coding/Graphics/raymarch-dd`
- `/home/vega/Coding/Web/dudu-webserver`

If a playground case becomes important enough to keep, promote it into one of
the tracked locations:

- `tests/fixtures/` for small deterministic compiler/LSP behavior
- `examples/` for user-facing examples
- `benchmarks/` for performance coverage
- `scripts/probe_optional.sh` or related probe scripts for optional real-library
  compatibility

Generated outputs belong under ignored build/cache directories, not source
directories.
