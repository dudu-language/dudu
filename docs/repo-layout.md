# Repository Layout

This repository is the Dudu compiler, tooling, docs, examples, and tests. It
should not contain personal scratch projects or generated playground build
output.

## Top Level

- `src/tools/`: tiny executable entry points for `dudu`, `duc`, and `dudu-lsp`.
- `src/dudu/`: shared implementation, split by subsystem:
  `core`, `frontend`, `parser`, `sema`, `codegen`, `native`, `project`, `lsp`,
  `format`, `testing`, and `support`.
- `include/dudu/`: public C/C++ headers exported by Dudu when needed.
- `tests/`: C++ unit tests and `.dd` fixtures for compiler and LSP behavior.
- `examples/`: curated Dudu examples that should stay reproducible.
- `benchmarks/`: generated or curated compiler benchmark inputs.
- `scripts/`: build, test, probe, install, and developer helper scripts.
- `editors/`: editor integrations, including the VS Code extension.
- `docs/`: language, compiler, project-driver, LSP, and planning documents.

The `src/tools/` plus `src/dudu/` split is intentional. Tool entry points should
stay tiny; the actual compiler, project driver, formatter, native scanner, and
language server code lives under `src/dudu/` and is currently built as the
`dudu_frontend` library.

The first subsystem directory split and `dudu-lsp` binary are complete. Future
cleanup should continue from [Repository Refactor Plan](repo-refactor-plan.md),
especially splitting the single implementation library into explicit subsystem
libraries when that pays off.

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
