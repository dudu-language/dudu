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
- Added class-scoped constants and class-scoped methods without `self`.
- Added Dudu-native operator methods through `@operator(...)`.
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
- Tightened semantic checks for imported C++ template function calls such as
  `namespace.identity[i32](value)`.
- Added local playground guidance for `dudu run` and `dudu test` experiments.
- Improved native-header scanner failure hints for missing include paths and
  broken Clang tooling.
- Improved native const modeling for C++ `T const*`/`T const&` signatures and
  pointer-to-const calls.
- Added `duc clean-cache`/`dudu clean-cache` for clearing native-header scan
  cache files.
- Added unchanged-build skipping for direct native builds when generated C++,
  native command, and native source inputs are unchanged.
- Added aliased lowercase native macro imports, such as `cassert.assert(...)`.
- Added variadic native macro passthrough for fixed-leading-argument macros.
- Added a macro-bomb fixture and example for imported C/C++ macro coverage.
- Added the remaining language-completion checklist to `docs/le_plan.md`.
- Added POSIX mmap and pthread checks to the optional real-library probe.
- Added mutable class static fields with Python-style class-level annotated
  assignments.
- Fixed aliased native C/C++ record types such as `rl.Vector2` resolving as
  aliases of their underlying header types during overload checks.
- Added opt-in developer dependency setup for local raylib/SDL3 probe installs
  under the ignored `third_party/` directory.
- Split core repository validation from optional package-SDK native probes with
  `scripts/test_full.sh` for running both tiers.
- Added native semantic-token classification for scanned C/C++ symbols in the
  language server.
- Added user-owned CMake project build/run support through `[cmake] source`
  and `[cmake] target`.
- Added CTest-backed `dudu test` support for user-owned CMake projects.
- Added LSP hover for Dudu symbols reached through imported module aliases.
- Added parsed `TypeRef` storage to function signatures so call checking can
  prefer structured expected argument and return types.
- Added parsed `TypeRef` preservation for Dudu-native generic method
  signatures.
- Added parsed `TypeRef` preservation for inherited and abstract method
  signature matching.
- Added parsed `TypeRef` substitution for generic Dudu field member lookup.
- Removed render-and-reparse churn from Dudu-native generic class
  instantiation.
- Removed obsolete string-based Dudu method/class template substitution helpers.
- Shared parsed `TypeRef` index and iterable inference for public string entry
  points before falling back to native/operator boundaries.
- Added Cairo to the optional native compatibility probe suite.
- Added CMake install rules and `scripts/install-local.sh` for local source
  installs of `dudu`, `duc`, docs, and editor support.

### Changed

- Expanded `docs/le_plan.md` with explicit prototype-cruft cleanup rules for
  vacuous helpers, one-line wrappers, temporary compile-shape branches, and
  behavior-preserving style passes.
- Made LSP rename declaration-anchored across the workspace and allowed
  current-document edits from an unqualified call site when the call resolves
  to one renameable Dudu declaration in that document.
- Made declaration-anchored LSP workspace rename skip files that declare their
  own same-named Dudu symbol until full cross-file symbol identity is available.
- Made declaration-anchored LSP references skip unrelated same-named
  redeclarations while preserving workspace references through imported module
  paths.
- Covered ambiguous LSP missing-import candidates so quick fixes stay limited
  to unambiguous workspace symbols.
- Made project-driver `dudu fmt` skip generated/build directories while leaving
  direct `duc fmt <dir>` recursion unchanged.
- Shared structured leading-import organization between the formatter and LSP
  organize-imports code action.
- Included optional real-header LSP probes in `scripts/test_full.sh`.
- Added project-driver output for successful `dudu check`, documented
  `dudu bench` in help, and made `dudu bench --quiet`/`--help` behave as
  project-driver flags while preserving benchmark arguments after `--`.
- Improved native overload diagnostics to show argument types and candidate
  signatures.
- Included local header path, size, and mtime in native-header cache keys so
  edited wrapper headers rescan cleanly.
- Improved native header scanning for direct SIMD intrinsic imports.
- Removed generated `CHANGELOG.md` files from `dudu init` and `dudu new`
  scaffolds.
- Updated public docs to prefer `dudu` for project-driver workflows and keep
  `duc` focused on explicit compiler-driver workflows.
- Documented `dudu build`, `dudu run`, and `dudu test` as the stable project
  front door, with direct and CMake backends as implementation details.
- Added fast validation that `dudu init` and `dudu new` create runnable hello
  projects with the documented git/`.gitignore` behavior.
- Added an initial user-owned `CMakeLists.txt` to `dudu init` and `dudu new`
  scaffolds; it builds generated Dudu module artifacts through `duc`.
- Made scaffolded `CMakeLists.txt` files depend on all `src/*.dd` files so
  imported Dudu source edits trigger regeneration.
- Made direct `dudu build` and `dudu run` print separate `analyze`, `emit`,
  `compile`, and final `output`/`run` stages.
- Made generated and user-owned CMake backend stage logs use project-driver
  timing prefixes for `generate`, `configure`, and `compile`.
- Reused the loaded module graph while emitting generated CMake projects so
  source dependency discovery no longer walks imports a second time.
- Split AST-backed unused-local and shadowing lint logic into a focused
  language-server scope-lint module.
- Split AST-backed suspicious narrowing-cast lint logic into a focused
  language-server lint module.
- Clarified native-header redeclaration collision handling by spelling the
  opaque native type exception at the collision branches.
- Made direct unaliased native C/C++ functions participate in current-document
  LSP reference lookup once the native scanner proves the symbol exists in the
  document's imports.
- Kept Dudu-owned LSP declaration/reference scope checks on Dudu-only symbols
  so ordinary references avoid native-header scanner work.
- Relaxed the LSP smoke-test subprocess guard from 5s to 10s so the dense
  JSON-RPC batch does not fail on normal timing variance while still catching
  hangs quickly.
- Documented the broader C++ standard-library interop fixture in the native
  compatibility matrix, including `std.function`, smart pointers, span, and
  container coverage.

### Fixed

- Switched generated-CMake `dudu test` builds to per-module test artifacts
  plus a small generated harness instead of one merged test translation unit.
- Decoded common escaped characters in quoted `dudu.toml` strings, including
  escaped quotes and backslashes in scalar fields and arrays.
- Rejected invalid or unfinished escapes in quoted `dudu.toml` strings instead
  of silently dropping the backslash.
- Kept `duc bench` argument forwarding transparent while reserving
  `--quiet`/`--help` project-driver parsing for `dudu bench`.
- Fixed delegated `[test]` and `[bench]` project commands so they run from the
  manifest directory, including when invoked from a project subdirectory.
- Sped up native header AST parsing so standard-library imports no longer hang
  `test_codegen_shapes.sh`.
- Added explicit diagnostics for selective Dudu imports that collide with local
  declarations or earlier selective imports.
- Improved cyclic Dudu module import diagnostics to show the module path that
  closes the cycle.
- Rejected runtime `assert` in freestanding and embedded target modes instead
  of emitting hosted exception machinery.
- Fixed native signature parsing for signatures with suffixes such as
  `noexcept(true)`.
- Fixed `size_t` native type mapping to `usize`.
- Fixed language-server semantic highlighting so native-header scan failures do
  not blank ordinary Dudu tokens.
- Rejected Python-style dunder names on free functions as well as methods.
- Fixed language-server go-to-definition for imported C/C++ headers found
  through manifest-relative `[include] paths`.
- Rejected unsafe merged C++ output for Dudu modules that declare the same
  module-local type name, including explicit `--emit-cpp` and header emission.
- Added regression coverage that generated-CMake module builds allow
  same-named functions in different modules while merged C++ output rejects
  that unsafe shape.
- Improved language-server references for aliased native functions such as
  `dudu_native.dudu_native_add`.
