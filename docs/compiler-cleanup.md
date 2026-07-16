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

Outcome:

- removed the public `build_config_path` alias and four private forwarding
  wrappers that only renamed project configuration calls
- retained target selection and dependency fetching in the command/test entry
  paths where those effects are visible
- command-family ownership remains a separate cleanup item because
  `cli_command.cpp` still combines dispatch, module emission, artifact
  manifests, project builds, formatting, and benchmarks

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

### Parser, semantic analysis, and code generation

- remove duplicate type/expression interpretation and forwarding helpers
- keep parser recovery in parser ownership and semantic decisions in sema
- keep raw source rewriting confined to explicit C++ escape lowering
- split files over the size guideline when they combine distinct behavior

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
