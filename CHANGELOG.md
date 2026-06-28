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
- Added `scripts/bench_compiler.sh --build-type` so compiler throughput can be
  measured separately for Debug development builds and Release shipped-tool
  builds.
- Expanded generated compiler benchmark shapes with calls, control-flow,
  arrays/indexing, and generics stress cases.
- Reduced parser/AST memory churn by storing `SourceLocation` file names as
  strings instead of full `std::filesystem::path` objects.
- Reduced parser allocation churn by avoiding tuple-item vector allocation for
  non-comma expressions and pre-reserving lexer token storage.
- Reduced lexer token memory churn by storing token text as views into the
  parsed source and copying only at AST persistence boundaries.
- Increased lexer token pre-reservation for token-dense source so expression
  stress files spend less of the load phase growing the token vector.
- Reduced source-location memory churn by interning immutable filename storage
  across tokens and AST ranges instead of copying the path into every location.
- Reduced statement parser work by scanning once for top-level declaration and
  assignment operators instead of walking the same statement token span twice.
- Switched interned source filename storage from a tree set to a hash set to
  reduce source-location lookup overhead without changing stored filename
  lifetime.
- Moved rare expression-attached type metadata behind pointer storage, shrinking
  `Expr` and `Stmt` nodes and reducing peak RSS across generated compiler
  benchmarks.
- Removed the unused expression `params` payload and its dead AST walker passes,
  further shrinking expression-heavy compiler memory use.
- Moved sparse statement-declared type metadata behind pointer storage,
  shrinking `Stmt` nodes and reducing peak RSS across generated compiler
  benchmark shapes.
- Moved sparse expression template type arguments behind pointer storage,
  shrinking expression-heavy AST memory use without changing template-call
  semantics.
- Moved sparse assertion message expressions behind pointer storage, shrinking
  statement nodes and reducing peak RSS across generated compiler benchmark
  shapes.
- Moved sparse match guard expressions behind pointer storage, further reducing
  statement-heavy AST memory use.
- Moved sparse match pattern expressions behind pointer storage, shrinking
  statement nodes again while keeping match handling structured.
- Moved sparse `for` iterable expressions behind pointer storage, reducing AST
  memory for non-loop-heavy code.
- Moved statement condition expressions behind pointer storage, reducing
  statement node size while keeping control-flow, match, and assertion checks
  structured.
- Reduced parser work by carrying layout-token metadata on joined token spans
  instead of rescanning each expression/type piece before parsing it.
- Removed obsolete string-based Dudu method/class template substitution helpers.
- Removed an unused unknown-expression parser helper and renamed the remaining
  text call parser as an explicit `cpp(...)` escape-boundary helper.
- Removed the raw line scanner from LSP reference query selection; references
  now use parsed AST symbols and expression paths instead of arbitrary dotted
  source text, and the leftover dotted-symbol character helper was deleted.
- Shared Dudu constructor/destructor method-name predicates between sema and
  C++ class emission through the naming module.
- Split LSP cursor selection into a shared AST-backed selection pass so
  definition and reference queries can reuse the selected symbol, dotted path,
  and expression path instead of reparsing for each lookup question.
- Made the LSP hover request path reuse the shared cursor selection expression
  path instead of parsing the document again for member hover.
- Made LSP reference and rename scope checks consume the shared cursor
  selection result instead of selecting the same symbol/path again.
- Made LSP rename call-site detection use the shared cursor selection result
  instead of reparsing the document to check whether the cursor is on a call
  callee.
- Split LSP reference collection behind its own API header so navigation no
  longer owns reference-search declarations.
- Added an AST-backed `references_in` entry point so LSP reference and rename
  paths can reuse parsed modules instead of reparsing inside reference
  collection.
- Added explicit visible-document symbol collection for LSP reference and
  rename scope checks, keeping project-wide symbol queries separate from
  per-file reference filtering.
- Fixed LSP member-call selection so references and rename preserve full
  callee paths such as `module_alias.function` when the cursor is on the member
  name.
- Made LSP references and rename derive cursor selection from the same visible
  parsed module unit used for their symbol and reference scans.
- Made LSP definition lookup reuse a loaded module tree for header imports,
  cursor selection, current-file symbols, and Dudu import resolution before
  loading native-aware state for member/native fallbacks.
- Made LSP hover reuse the loaded module tree for normal/imported symbols and
  load native-aware state only for native symbol/member fallback hover.
- Removed the LSP symbol API that parsed documents internally; symbol request,
  completion, quick-fix, and reference paths now load or parse modules
  explicitly before collecting symbols.
- Removed document-parsing LSP selection wrappers; hover now derives cursor
  selection from the visible module it already loaded.
- Made LSP completion and signature help reuse one loaded module tree per
  request instead of loading again inside module/member/symbol helpers.
- Made LSP local-type lookup consume a loaded visible module unit instead of
  parsing the document inside local-context helpers.
- Made LSP semantic tokens reuse the shared module loader and visible module
  unit instead of parsing and native-merging inside the request handler.
- Made LSP code actions reuse shared module loading and visible module units
  for organize-import and missing-import actions.
- Shared parsed `TypeRef` index and iterable inference for public string entry
  points before falling back to native/operator boundaries.
- Added Cairo to the optional native compatibility probe suite.
- Added CMake install rules and `scripts/install-local.sh` for local source
  installs of `dudu`, `duc`, docs, and editor support.
- Documented the planned Dudu doc comment/docstring model for LSP hover,
  completion, signature help, imported module docs, and native C/C++ docs.
- Added scalable generated-source compiler benchmark cases with line-count
  controls and lines-per-second summaries.
- Expanded compiler benchmarks with selectable generated code shapes for
  functions, classes, expression-heavy bodies, and multi-module import graphs.

### Changed

- Removed `[build] backend = "direct"` as a supported Dudu project backend;
  project builds use generated/user-owned CMake, while `duc build <file.dd>`
  remains the low-level compiler-driver build path.
- Removed the implicit single-file direct backend shortcut from `dudu build` and
  `dudu run`; generated/user-owned CMake is now the project build path even for
  small projects.
- Avoided cloning the full module symbol table for non-generic function and
  method body/declaration checks. On the local 5k generated functions
  benchmark, `duc check` dropped from about 3.5s to about 0.5s.
- Avoided loading the full module tree for low-level `duc check` on single-file
  sources without Dudu module imports. On the local 5k generated functions
  benchmark, `duc check` dropped further to about 0.22s; the 5k
  expression-heavy benchmark dropped to about 2.0s with much lower RSS.
- Avoided copying expression/type token slices during parsing when the token
  span contains no layout tokens. The local 5k expression-heavy parse/load
  phase dropped from about 0.87s to about 0.76s.
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
- Made lint remove-line code actions consume structured diagnostic
  `data.fixRange` edits produced from AST-backed lint passes instead of
  rebuilding delete ranges from document line text.
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
- Made generated CMake module emission depend on the parsed `dudu.toml`, so
  manifest-only changes can trigger Dudu re-emission instead of relying only on
  `.dd` source mtimes.
- Updated `dudu init`/`dudu new` starter CMake to compile all generated Dudu
  module `.cpp` files instead of only `main.cpp`.
- Made the `dudu init`/`dudu new` starter CMake refresh generated module
  artifacts through a stamp output so no-op CMake builds do not rerun Dudu
  emission.
- Listed generated starter-CMake module sources as custom-command byproducts so
  missing generated files can be recovered by the build graph.
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
- Split raw `cpp(...)` escape-hatch lint logic into a focused language-server
  lint module.
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
