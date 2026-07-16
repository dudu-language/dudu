# Compiler Cleanup

This document tracks the architecture, readability, and simplification pass
started from the `before_cleanup` tag at commit `4162b218`.

The pass is behavior-preserving unless it exposes a documented compiler bug.
It is not a formatting rewrite. A change belongs here when it removes
unnecessary machinery, gives a subsystem clearer ownership, or makes a
previously difficult path straightforward to inspect and debug.

## Baseline

Measured on July 16, 2026 from `before_cleanup`:

- tracked hand-written and generated compiler C++: 67,599 lines in 409 files
- complete `scripts/test_fast.sh`: 56.52 seconds
- peak RSS during that run: 713,176 KiB
- all 27 CTest executables and the LSP smoke, recovery, synchronization, and
  matrix checks passed

Generated protocol sources are tracked separately from hand-written size
guidelines. Large test programs should be split by fixture ownership, not by
arbitrary line count.

## Ownership

- `core`: source identities, AST data, structured type and expression facts
- `parser`: tokenization and source-shaped AST construction
- `project`: manifests, dependencies, module graph, project index, and build
  configuration
- `sema`: binding, inference, overload resolution, type checking, and typed
  semantic facts
- `codegen`: C++ lowering and emission from typed AST facts
- `native`: C/C++ scanning, native identities, metadata, overload matching,
  and cache serialization
- `macro`: typed declaration-macro protocol, worker lifecycle, and AST merge
- `lsp`: editor protocol and queries over parser/project/sema products
- `frontend`: command-line parsing and command dispatch
- `testing`: Dudu project test discovery and execution
- `support`: installed toolchain lifecycle and genuinely shared process/file
  support

Source spelling may cross these boundaries only for source display and ranges,
formatting/trivia, explicit `cpp(...)` escapes, macro wire data, and native
C/C++ spellings. Semantic decisions use AST nodes, `TypeRef`, symbol identity,
and typed facts.

## Cleanup Ledger

### Command and project configuration

- remove forwarding wrappers around `find_project_config`,
  `parse_project_config`, and option-specific configuration
- keep dependency fetching and target selection explicit at command entry
- split command dispatch only where command families have independent
  ownership
- keep `dudu` responsible for project/toolchain orchestration and invoke
  `duc` explicitly for compilation

Outcome:

- removed the public `build_config_path` alias and four private forwarding
  wrappers that only renamed project configuration calls
- retained target selection and dependency fetching in the command/test entry
  paths where those effects are visible
- split command ownership without adding a second dispatch framework:
  `cli_command.cpp` now routes commands, `cli_project_commands.cpp` owns
  project/build/dependency/format operations, and `cli_compile_command.cpp`
  owns checking, indexing, emission, and artifact manifests
- reduced the mixed 676-line dispatcher to 93 lines; the two owned command
  units are 230 and 410 lines
- preserved the cached module-emission short circuit before source loading
- renamed generated CMake's compiler input to `DUC_EXECUTABLE`
- made build, run, and test resolve the sibling or `PATH` `duc` binary instead
  of passing the `dudu` project driver back into generated compilation rules

Validation: project configuration tests, CLI help/smoke checks, project
backend tests, and dogfood `dudu build`.

### Native AST metadata reader

- split AST-dump context tracking, declaration extraction, and public scan
  entry points
- replace the monolithic declaration chain with named declaration handlers
- retain one generic scanner path without header- or library-specific rules

Outcome:

- moved parser state and context types into one internal boundary
- separated declaration extraction from AST-tree context/comment/parameter
  tracking
- replaced the ten-argument mutable parser call with `AstParseState`
- kept declarations generic; no C++ library or header names are encoded in the
  scanner
- confirmed the existing `std.vector.erase` fixture failure reproduces at
  `before_cleanup` and is not a cleanup regression

Validation: native frontend/type compatibility tests, compatibility fixtures,
GCC/libstdc++ and focused Clang/libc++ probes.

### Native header scanning and import projection

- keep Clang invocation, cache lifecycle, retry policy, and scan assembly in
  scanner ownership
- keep direct/aliased import visibility, native-name prefixing, and macro
  source metadata in import-view ownership
- remove visibility branches that cannot be reached by any supported import
  kind

Outcome:

- split the 705-line `native_headers.cpp` into a 380-line scanner/cache unit
  and a 335-line import-view unit
- retained the existing source-location matching and scanner retry behavior
- consolidated repeated six-collection scan merges behind one local helper
- removed `alias_visible_functions`, whose import-kind guard returned for
  every foreign import before its filtering branch could run
- kept aliasing generic; no library, header, or namespace special case was
  introduced

Validation: native frontend/type compatibility/member tests, native
compatibility and standard-library fixtures, and complete `scripts/test_fast.sh`
in 68.75 seconds with 683,636 KiB peak RSS.

### Parser, semantic analysis, and code generation

- remove duplicate type/expression interpretation and forwarding helpers
- keep parser recovery in parser ownership and semantic decisions in sema
- keep raw source rewriting confined to explicit C++ escape lowering
- split files over the size guideline when they combine distinct behavior

Outcome:

- separated structural native type normalization from compatibility
  normalization, so template-aware member lookup retains arguments while
  assignment compatibility can still collapse equivalent C++ spellings
- fixed native member lookup for aliases such as
  `std.basic_string[char] -> std.string` without encoding standard-library
  names in overload resolution
- added one semantic boundary for native types exposed by imported Dudu
  modules; it follows the reachable module graph and imports only types that
  cross Dudu declarations instead of leaking native functions, values,
  macros, or unrelated implementation classes
- fixed member lookup on native types returned through another Dudu module,
  including `list[std.thread]` and indexed `.join()` calls
- moved index-operator hook lowering out of the general call emitter and gave
  reads, writes, and compound writes one receiver/type-resolution path
- removed the duplicate index-hook declarations and the stale overload that
  no longer matched the implementation
- moved enum payload classification from call emission into semantic enum
  support, where match and expression lowering can share the structured fact
- reduced `cpp_expr_call_emit.cpp` from 744 to 512 lines while keeping the
  extracted index-hook unit at 236 lines

Validation: parser ranges, AST/type/shape, inference, module, emission,
negative, and canonical fixture suites.

### Language server

- keep diagnostics, navigation, references, completion, hover, semantic
  tokens, and inlay hints as queries over shared compiler products
- remove repeated symbol-selection or source-path policy
- split implementation files by LSP capability when they mix unrelated query
  behavior

Validation: deterministic LSP tests plus invalid-edit, incremental, and
dogfood latency checks.

### Macros

- keep generated protocol code generated
- remove duplicate bridge conversion, worker setup, and cache policy
- keep source fallback locations limited to diagnostics for declarations
  generated without their own source range

Validation: protocol, syntax, worker, AST bridge, package, expansion, and SDK
compile tests.

### Tests, scripts, and build tooling

- split oversized hand-written test drivers into cohesive fixture groups
- split shell scripts into named libraries only when those libraries have
  independent jobs
- remove stale generated build trees and untracked artifacts from audits
- keep heavyweight native/performance tests outside the fast default loop

Validation: complete fast suite, canonical fixture execution, negative tests,
site checks, and relevant packaging/build probes.

## Completion Gate

The pass is complete when:

- each subsystem above has been audited and its ledger outcome recorded
- no known pointless wrapper, always-constant helper, duplicate semantic path,
  obsolete compatibility route, or speculative fallback remains
- materially oversized hand-written files either have one cohesive reason to
  remain large or are split along ownership boundaries
- full fast and canonical fixture suites pass
- `raymarch-dd` and `dudu-webserver` build with the cleaned compiler
- generated C++, diagnostics, editor behavior, public syntax, and measured
  user-visible latency have not regressed
- stable green milestones are committed and pushed

## Known Baseline Failures

These failures reproduce at `before_cleanup` and remain completion work rather
than cleanup regressions:

- `cpp_stdlib_algorithms.dd`: `std.vector.erase(first, last)` loses the pointee
  type in the scanned iterator parameter
