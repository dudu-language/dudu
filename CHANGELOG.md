# Changelog

## [Unreleased]

### Added

- Added the Dudu project driver plan.
- Added `dudu init` and `dudu new` project scaffolding commands.
- Added `dudu init <path>` support.
- Added Cargo-style git initialization for project scaffolds.
- Added support for the newer `dudu.toml` project-driver shape:
  `entry`, `[cxx]`, `[include]`, `[sources]`, `[pkg]`, `[link]`, and
  `[build].dir`.
- Added C and C++ source passthrough for native builds.
- Added `@test` functions and generated native `dudu test` harnesses.
- Added runtime `assert` lowering for test-friendly failures.
- Added `docs/tests.md` with the Cargo-style test direction.
- Added `docs/le_plan.md` as the next implementation roadmap.
- Added `dudu clean` for removing a project's configured build directory.
- Added Cargo-ish stderr step logs for `dudu build`, `dudu run`, `dudu test`,
  and `dudu cmake`.
- Added named project targets through `[targets.<name>]` for build, run, test,
  and CMake generation.
- Added recursive `dudu test ./...` and directory test discovery.
- Added unique generated test binary paths under `build/dudu-tests/`.
- Added class-scoped constants and `@staticmethod` methods.
- Added Dudu-native operator methods such as `__add__` and `__eq__`.
- Improved native header scanning for pointer and reference parameter types.
- Improved `dudu test` zero-test output.
- Added custom runtime assertion messages with `assert expr, "message"`.
- Added `@test.ignore` and `@test.should_panic` test decorators.
- Added imported C++ inheritance awareness for inherited method lookup and
  derived-to-base pointer/reference calls.
- Added clearer native-header scan diagnostics when project-relative C/C++
  headers are missing or cannot be parsed by Clang.
- Added default `dudu test` output capture plus `--no-capture`/`--nocapture`.
- Added native header support for namespaced and nested C++ class names.
- Added `scripts/test_fast.sh` for normal inner-loop compiler validation.
- Added `debug_assert` for native C/C++ `assert(...)` semantics.
- Added C++ exception interop with `try`, `except`, and `raise`.
- Added aliased lowercase native macro imports, such as `cassert.assert(...)`.
- Added variadic native macro passthrough for fixed-leading-argument macros.
- Added a macro-bomb fixture and example for imported C/C++ macro coverage.
- Added the remaining language-completion checklist to `docs/le_plan.md`.

### Changed

- Improved native overload diagnostics to show argument types and candidate
  signatures.
- Improved native header scanning for direct SIMD intrinsic imports.
- Removed generated `CHANGELOG.md` files from `dudu init` and `dudu new`
  scaffolds.
- Updated public docs to prefer `dudu` for project-driver workflows and keep
  `duc` focused on explicit compiler-driver workflows.

### Fixed

- Sped up native header AST parsing so standard-library imports no longer hang
  `test_codegen_shapes.sh`.
- Rejected runtime `assert` in freestanding and embedded target modes instead
  of emitting hosted exception machinery.
- Fixed native signature parsing for signatures with suffixes such as
  `noexcept(true)`.
- Fixed `size_t` native type mapping to `usize`.
