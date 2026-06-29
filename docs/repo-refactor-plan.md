# Repository Refactor Plan

Dudu has grown from one compiler executable into a language project with several
deliverables:

- `duc`: low-level compiler driver
- `dudu`: project driver and normal user front door
- language server
- editor integrations
- public C/C++ headers
- examples, tests, benchmarks, and docs

The current implementation mostly lives as one flat `src/dudu/` pile plus a
single `src/main.cpp` used by both binaries. That was acceptable while
bootstrapping, but it now hides subsystem boundaries and makes future compiler
work harder to navigate.

The refactor should be disruptive enough to make the repository clean, but not
behaviorally disruptive. Move files by subsystem, keep namespaces stable, and
validate after each mechanical step.

## Target Layout

```text
src/
  dudu/
    core/
    frontend/
    parser/
    sema/
    codegen/
    native/
    project/
    lsp/
    format/
    testing/
    support/
  tools/
    duc.cpp
    dudu.cpp
    dudu_lsp.cpp
include/
  dudu/
editors/
  vscode/
  vim/
  nvim/
tests/
examples/
benchmarks/
scripts/
docs/
```

Keep the C++ namespace as `dudu`. The folder split is an implementation
organization change, not a namespace rename.

## Subsystem Buckets

### `src/dudu/core/`

Small shared data structures and utilities used everywhere:

- AST/type/expression model once the parser-specific pieces are separated
- tokens, source locations, source interning
- naming helpers
- common file I/O
- control-flow helpers when not tied to sema

Candidate current files:

- `ast*`
- `token*`
- `source*`
- `naming*`
- `file_io*`
- `control_flow*`
- `array_shape*`
- `match_patterns*`

### `src/dudu/parser/`

Lexing and parsing only:

- lexer
- module parser
- declaration parser
- statement parser
- expression/type token parsers
- parser utilities

Candidate current files:

- `lexer*`
- `parser*`
- `ast_expr_parse*`
- `ast_expr_token_*`
- `ast_type_parse*`
- `ast_type_token_parser*`
- `ast_parse_*`

### `src/dudu/sema/`

Semantic analysis and type checking:

- declarations
- expressions
- assignment
- body checking
- generics
- inheritance
- operators
- match/sum types
- builtin methods
- constexpr

Candidate current files:

- `sema*`
- `type_compat*`
- `unsupported*`
- semantic-only pieces of `module_import_aliases*` if they are not pure module
  graph helpers

### `src/dudu/codegen/`

C++ emission:

- C++ source/header/module emission
- expression/statement lowering
- C++ type lowering
- test harness emission
- raw `cpp(...)` escape emission

Candidate current files:

- `cpp_*`

The directory should be named `codegen`, not `cpp`, because later Dudu may grow
other backends. The files can still keep `cpp_` prefixes until a later naming
pass.

### `src/dudu/native/`

C/C++ interop and native header scanning:

- native header scanner
- native caches
- native identity
- native signature matching/substitution
- native build/package config helpers

Candidate current files:

- `native_*`

### `src/dudu/project/`

Project model and build driver:

- project config
- project driver
- generated CMake backend
- ProjectIndex and ProjectIndex cache
- module loader and module naming

Candidate current files:

- `project_*`
- `cmake_*`
- `module_*`
- `build_flags*` if kept with project configuration

### `src/dudu/lsp/`

Language server implementation:

- JSON-RPC
- request handlers
- diagnostics
- hover
- definition
- references/rename
- completion/signature help
- semantic tokens
- code actions
- workspace/document support
- LSP-specific lints

Candidate current files:

- `language_server*`

### `src/dudu/format/`

Formatting and import organization:

- formatter
- format path/check support
- import organization used by formatter/LSP code actions

Candidate current files:

- `format*`

### `src/dudu/frontend/`

High-level compiler-driver API:

- CLI option parsing
- CLI command dispatch
- usage/version
- public `duc`/`dudu` frontend entry APIs

Candidate current files:

- `cli_*`
- top-level orchestration that does not belong to project driver directly

### `src/dudu/testing/`

Compiler-owned test harness support used by `dudu test`:

- test discovery
- test driver helpers

Candidate current files:

- `test_driver*`

### `src/tools/`

Small executable entry points:

- `src/tools/duc.cpp`
- `src/tools/dudu.cpp`
- `src/tools/dudu_lsp.cpp`

These files should stay tiny and should not contain compiler logic.

## Binary Shape

### Keep `duc`

`duc` remains the low-level compiler driver, like `rustc` under Cargo.

It should keep commands such as:

- `duc check`
- `duc emit`
- `duc emit-modules`
- `duc emit-test-modules`
- `duc fmt`

It should not own language-server startup. There are no existing Dudu users to
protect, so keeping a second `duc lsp` spelling would add avoidable toolchain
surface area.

### Keep `dudu`

`dudu` remains the user-facing project driver:

- `dudu init`
- `dudu new`
- `dudu build`
- `dudu run`
- `dudu test`
- `dudu fmt`
- `dudu clean`
- `dudu clean-cache`

### Add `dudu-lsp`

Add a dedicated language-server executable named `dudu-lsp`.

This is the clean toolchain shape:

- editor settings can point directly at a language server binary
- logs/process names clearly show `dudu-lsp`
- packaging can install editor tooling without pretending the compiler driver is
  the language server
- future LSP-only startup flags do not crowd `duc`

Remove `duc lsp` as part of this refactor. The VS Code extension and LSP scripts
should call `dudu-lsp` directly.

### Is LSP As A Compiler Mode Normal?

It is common but not the cleanest final shape.

Examples:

- `rust-analyzer` is a dedicated language-server binary, separate from `rustc`.
- `clangd` is a dedicated language-server binary, separate from `clang`.
- TypeScript uses `tsserver`, separate from `tsc`.
- Some smaller languages start with `compiler lsp` because it is simple and
  shares code easily.

For Dudu, `duc lsp` was a good bootstrap. A serious unreleased toolchain should
replace it with `dudu-lsp` while keeping the shared compiler library underneath.

## CMake Refactor

The top-level `CMakeLists.txt` should stop listing every source file inline.
Split source lists by subsystem to make ownership visible.

Target shape:

```cmake
add_library(dudu_core ...)
add_library(dudu_parser ...)
add_library(dudu_sema ...)
add_library(dudu_codegen ...)
add_library(dudu_native ...)
add_library(dudu_project ...)
add_library(dudu_lsp_lib ...)
add_library(dudu_frontend ...)

add_executable(duc src/tools/duc.cpp)
add_executable(dudu src/tools/dudu.cpp)
add_executable(dudu-lsp src/tools/dudu_lsp.cpp)
```

The first pass may keep one `dudu_frontend` library with subsystem source-list
variables if splitting link targets creates too much dependency churn. The end
state should make dependencies explicit enough that LSP-only code is not in the
minimal compiler frontend unless it is intentionally shared.

## Migration Sequence

1. Add tiny separate tool entry points.
   - Create `src/tools/duc.cpp`, `src/tools/dudu.cpp`, and
     `src/tools/dudu_lsp.cpp`.
   - Add an explicit `run_lsp_cli()` or equivalent library entry point.
   - Remove `duc lsp` from CLI parsing and usage.
   - Install `dudu-lsp`.

2. Move files mechanically by subsystem.
   - Use `git mv`.
   - Keep header include paths as `#include "dudu/..."` at first by making the
     include root continue to expose moved headers.
   - Update CMake source lists.
   - Build after each bucket.

3. Split CMake source ownership.
   - Replace the single huge `add_library(dudu_frontend ...)` source list with
     subsystem variables or subsystem libraries.
   - Keep public include behavior stable.

4. Split internal headers if useful.
   - Keep public install headers under `include/dudu`.
   - Keep private implementation headers under subsystem directories.
   - Do not expose private LSP/native/codegen headers as public API.

5. Update scripts and docs.
   - `install-local.sh` should install/symlink `dudu-lsp`.
   - VS Code extension should use `dudu-lsp`.
   - LSP scripts should call `dudu-lsp` directly.
   - Remove `duc lsp` references from docs, scripts, tests, and editor settings.

6. Optional later cleanup.
   - Rename files after the directory split if prefixes become redundant.
   - Example: `language_server_hover.cpp` can stay as-is initially; later it may
     become `src/dudu/lsp/hover.cpp`.

## Validation

After each phase:

- `cmake --build build -j$(nproc)`
- `./scripts/test_fast.sh`
- `./scripts/check_ast_migration_guards.sh`

After LSP binary work:

- `dudu-lsp` initializes over JSON-RPC
- VS Code extension can start the configured server

Before pushing larger file-move milestones:

- `dudu build --timings` in `raymarch-dd`
- `dudu build --timings` in `dudu-webserver`
- optional `scripts/test_full.sh` if the refactor touches native scanner or LSP
  behavior, not just file paths

## Non-Goals

- Do not rename the C++ namespace away from `dudu`.
- Do not rewrite compiler architecture during the mechanical move.
- Do not change Dudu language behavior.
- Do not flatten all files into `src/`.
- Do not keep stale compatibility aliases such as `duc lsp`.
