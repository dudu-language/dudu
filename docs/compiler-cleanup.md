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
- all 28 CTest executables and the LSP smoke, recovery, synchronization, and
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
- consolidated CLI input-to-source-root resolution into one command boundary,
  so check, index, emit, and native-cache cleanup cannot drift on relative
  input handling
- separated source-stamp persistence and invalidation from project graph
  construction; canonical paths, file stamps, cache records, and change
  detection now have one private project-index boundary
- reduced `project_index.cpp` from 566 to 418 lines without exposing filesystem
  implementation through the public project index API
- separated manifest parsing from target application, configuration discovery,
  and project-relative path policy; `project_config.cpp` is now 75 lines and
  the 446-line parser has one explicit owner
- made the manifest parser use shared `core/text` trimming and consolidated
  native build entries for root and named targets through one parser
- removed the always-true `parse_true` helper; capability flags now validate
  directly without pretending to compute a value
- moved running-executable and `PATH` command resolution into one support
  boundary used by `dudu`/`duc` pairing, standard-library discovery, macro
  runtime discovery, and native compiler cache identity
- removed four independent platform/path searches, including duplicate Linux
  `/proc/self/exe` and macOS `_NSGetExecutablePath` implementations

Validation: project configuration tests, CLI help/smoke checks, project
backend tests, complete `scripts/test_fast.sh` in 70.56 seconds with
737,924 KiB peak RSS, and dogfood builds for raymarch, webserver, raylib,
SDL3/ImGui, tensor indexing, BLAS, OpenCL, and autograd targets.

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
- removed the stale `merge_native_header_types` entry point; the scanner has
  one accurately named `merge_native_headers` operation for all native
  metadata
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
- removed expression-presence, decorator-string, receiver-class, and
  class-lookup aliases that only renamed existing structured AST or semantic
  operations
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
- separated token-span collection, source spelling reconstruction, and
  expression/type subparser handoff from top-level grammar and recovery
- reduced `parser.cpp` from 646 to 430 lines; the owned syntax-piece bridge is
  217 lines and introduces no second parser or source-string semantic path
- replaced three token end-position implementations with one core token
  operation, including correct line and column ranges for multiline tokens
- moved template-call lowering out of the general expression dispatcher;
  allocation, pointer-cast, layout, shape-assumption, generic-constructor, and
  native C++ template calls now share one owned structured path
- moved wrapper-type classification from parser utilities into the core type
  model, so code generation does not depend on parser implementation helpers
- reduced `cpp_expr_emit.cpp` from 626 to 445 lines; the extracted
  template-call unit is 203 lines and preserves the existing `Expr` and
  `TypeRef` contracts
- separated generated-name, imported-symbol, native-alias, and public-ABI
  projection from module artifact rendering and file orchestration
- made singleton and multi-module emission share one span-based path, removing
  six temporary copies of the complete root `ModuleAst` and duplicate regular
  and test emission branches
- removed the unused `write_cpp_module_artifacts` forwarding API
- reduced `cpp_emit_modules.cpp` from 644 to 381 lines; its 260-line context
  unit now owns the policy used to construct `CppEmitOptions`
- corrected project-backend fixtures that still configured the removed
  `DUDU_EXECUTABLE` CMake input instead of `DUC_EXECUTABLE`; the stale name had
  allowed those fixtures to fall back to an unrelated installed compiler
- separated class method emission from class layout and ordering; constructor
  and `super.init` handling, receiver substitution, virtual/abstract policy,
  operator names, method signatures, bodies, and out-of-line definitions now
  have one owner
- consolidated three copies of C++ template-parameter rendering and duplicate
  header-visibility predicates behind one declaration-emission helper shared
  by free functions, classes, and enum methods
- reduced `cpp_emit_classes.cpp` from 618 to 249 lines; the class-method unit is
  373 lines, and the shared declaration helper is 34 lines
- separated free-function declaration and body emission from generated-artifact
  assembly; decorators, signatures, variadic packs, generic/header ownership,
  test filtering, and C ABI declarations now have one free-function owner
- moved class forward declarations into class emission, leaving
  `cpp_emit.cpp` responsible for ordering header, source, module, and test
  artifact sections rather than implementing declaration policy
- reduced `cpp_emit.cpp` from 553 to 272 lines; the free-function emission unit
  is 286 lines and the class layout unit is 265 lines
- moved function/class decorator lookup and test-function classification into
  core decorator support, where sema, codegen, module ABI projection, and test
  discovery now consume the same declaration facts
- removed five subsystem-local decorator forwarding wrappers and two duplicate
  test classifiers, including the codegen-prefixed APIs that exposed no
  code-generation policy
- collapsed argument-only and expected-return generic method inference onto one
  enum/class/base traversal while preserving their distinct typed binding
  policies
- consolidated variadic, duplicate-parameter, parameter-type, and return-type
  declaration checks across enum methods, class methods, and free functions
- reduced `sema_methods.cpp` from 597 to 560 lines and
  `sema_declarations.cpp` from 575 to 549 lines without adding another semantic
  abstraction layer
- extended the structured native type model for associated member templates
  and compiler type transforms instead of interpreting dependent C++ spellings
  during overload matching or emission
- made partial-specialization matching bind scalar, value, template-template,
  and variadic pack parameters through one typed binding model, including
  well-formedness requirements and static constexpr values
- completed native scan qualification after declaration collection, so types
  referenced before their declaration receive the same scoped identity as
  types encountered later in the AST dump
- canonicalized native `size_type` and `difference_type` aliases to `usize`
  and `isize` after associated-type resolution; ordinary Dudu integer
  assignment remains strict
- restricted Clang-USR deduplication to declarations with the same imported
  binding, preserving distinct aliases that share a canonical native entity
- separated native partial-specialization matching, requirement checks,
  binding substitution, and constexpr value extraction from associated member
  type resolution; the two semantic units are 290 and 308 lines instead of one
  602-line mixed implementation
- split Clang AST-dump ingestion into type spelling/normalization, scan support,
  declaration extraction, and line orchestration; the former 755-line parser
  is now a 296-line orchestrator with 185-line type and 291-line support units
  behind the same internal scan API
- consolidated C/C++ `struct`/`class`/`union` tag stripping into one native
  identity rule used by symbol collection, member lookup, import compatibility,
  and structural type compatibility
- moved generic source-text operations such as trimming, identifier checks,
  prefix/suffix checks, and top-level argument splitting into `core/text`;
  parser and semantic code no longer import code-generation ownership merely
  to use string helpers
- removed all upward parser, semantic-analysis, and code-generation includes
  from `src/dudu/core`
- collapsed duplicate fixed-array and shaped-type rendering/substitution onto
  the shared structured `TypeRef` renderer
- removed one-use ellipsis parser wrappers and an always-identical unknown-type
  branch

Validation: parser ranges, AST/type/shape, inference, module, emission,
negative, code-generation shape, and canonical fixture suites, plus
CLI/project smoke, generated-CMake backend fixtures, `raymarch-dd`,
`dudu-webserver`, and the complete `dudu-datascience` target API. The complete
fast suite remains green in 69.20 seconds with 710,404 KiB peak RSS.

### Language server

- keep diagnostics, navigation, references, completion, hover, semantic
  tokens, and inlay hints as queries over shared compiler products
- remove repeated symbol-selection or source-path policy
- split implementation files by LSP capability when they mix unrelated query
  behavior

Outcome:

- moved semantic-token record layout, legend order, modifiers, source-range
  insertion, sorting, and LSP delta encoding into one wire-format unit
- removed the second token record and delta encoder from invalid-source
  lexical highlighting; semantic and recovery paths now cannot silently drift
  to different protocol legends
- separated expression, member, local-binding, type, and statement token
  inference from module declaration traversal
- consolidated duplicate top-level and method parameter/body traversal behind
  one function collector while preserving implicit `self` typing
- reduced `language_server_semantic_tokens.cpp` from 717 to 272 lines; the
  expression/body collector is 396 lines and the shared wire implementation is
  83 lines
- moved editor-local binding, tuple destructuring, loop element inference,
  function generic setup, and `self` substitution into one LSP scope boundary
  shared by navigation and inlay hints
- made strict navigation inference and tolerant inlay inference explicit
  policies on that boundary, preserving native-project retry instead of
  masking an unresolved foreign call as `auto`
- removed inlay hints' second partial local type analyzer and made discard
  bindings follow the same semantic binding rule as other editor queries
- replaced source-line scanning for explicit variable and loop annotations
  with AST type presence, and distinguish synthesized `self` from an explicit
  receiver type through its parser-owned source location
- moved imported Dudu class projection into an owning presentation-symbol
  context, so editor type previews and method parameter hints no longer manage
  copied declaration lifetimes inside the inlay query
- reduced `language_server_inlay_hints.cpp` from 646 to 520 lines and
  `language_server_local_context.cpp` from 433 to 336 lines; their shared scope
  and imported-symbol ownership units are 122 and 61 lines
- separated signature help from completion and moved their shared generic-call
  and active-parameter detection into one token-based call-site query
- removed signature-help declarations and implementation from completion
  ownership; `language_server_completion.cpp` is now 408 lines, with the
  call-site and signature-help units at 99 and 148 lines
- consolidated their duplicated Markdown documentation serialization in the
  LSP JSON wire helper
- moved Content-Length framing, synchronized output, responses, errors,
  notifications, and server-initiated refresh requests into one JSON-RPC
  transport boundary
- removed transport state and eleven stale compiler/native/standard-library
  includes from the server session; `language_server.cpp` is now 533 lines and
  the transport unit is 93 lines
- separated native import-target navigation from ordinary AST and symbol
  definition dispatch; compiler and pkg-config include discovery, path
  resolution, and external process ownership now live in one native-header
  definition unit
- reduced `language_server_definition.cpp` from 617 to 456 lines; the
  native-header definition unit is 177 lines and exposes one capability-level
  query
- consolidated fenced-code and Markdown hover serialization in one LSP
  presentation helper instead of five local implementations
- moved primitive and native-alias hover construction into type-hover
  ownership, and moved native identity source-path extraction into the native
  lookup boundary
- reduced `language_server_hover.cpp` from 588 to 469 lines; it now dispatches
  hover queries without owning primitive type tables or native alias
  presentation
- made direct hover and inlay label parts share one type-presentation path for
  primitive documentation, native aliases, class previews, layouts, docs, and
  definition locations
- reduced `language_server_inlay_type_details.cpp` from 216 to 108 lines;
  inlay serialization remains local while type meaning and presentation live
  with type hover
- made workspace discovery consume `rootUri` and `workspaceFolders` from the
  client and manifest roots discovered from open documents
- stopped configless scratch documents from recursively indexing their parent
  directory; this removes duplicate unrelated modules from completion and code
  action queries without making scratch files unavailable
- added a dedicated workspace fixture covering explicit client roots,
  manifest-backed sibling discovery, and scratch-parent isolation
- made compiler and language-server macro diagnostics share one
  macro-range-to-source-location conversion instead of maintaining parallel
  location policy

Validation: deterministic LSP tests plus invalid-edit, incremental, and
dogfood latency checks, including decoded semantic-token and macro-decorator
assertions. The complete 28-target CTest suite and `scripts/test_fast.sh`
remain green; the latest bounded run took 69.65 seconds with 733,856 KiB peak
RSS.

### Macros

- keep generated protocol code generated
- remove duplicate bridge conversion, worker setup, and cache policy
- keep source fallback locations limited to diagnostics for declarations
  generated without their own source range

Outcome:

- kept generated protocol and SDK conversion code as the sole wire-format
  boundary; no handwritten shadow conversion path was introduced
- consolidated worker source and SDK artifact writes onto shared
  compiler-internal file I/O while keeping capability-checked macro file access
  separate
- made source and binary worker identities share canonical plan, toolchain, and
  build-input hashing without changing the existing hash order or cache keys
- replaced independent SDK and worker compilation thread loops with one bounded
  command runner that owns status collection, compiler logs, and failures
- computes module compile values once per expansion batch instead of reparsing
  the same build values for every macro invocation
- retained atomic temporary-and-rename publication independently for worker
  lookups, worker binaries, SDK entries, and expansion caches because those are
  distinct cache policies rather than duplicate file-writing helpers

Validation: protocol, syntax, worker, AST bridge, package, expansion, and SDK
compile tests.

### Tests, scripts, and build tooling

- split oversized hand-written test drivers into cohesive fixture groups
- split shell scripts into named libraries only when those libraries have
  independent jobs
- remove stale generated build trees and untracked artifacts from audits
- keep heavyweight native/performance tests outside the fast default loop

Outcome:

- replaced separate test-target, strict-warning, warning-as-error, and CTest
  registration lists with one `dudu_add_test` helper
- retained target-specific fixture definitions beside the targets that consume
  them instead of hiding those differences in the helper
- registered `dudu_macro_expansion_render_tests`, which the old duplicated
  lists built but accidentally omitted from CTest
- split the 1,732-line native frontend test executable by actual ownership:
  scan basics, import identity, scan deduplication, cache behavior, template
  metadata, and import projection now have independent targets between 168 and
  533 lines
- replaced the 1,532-line shell heredoc used for the LSP matrix with a five-line
  launcher and real Python modules for protocol handling, workspace fixtures,
  request construction, core editor assertions, native assertions, and code
  actions; every owned module is at most 466 lines
- split the 1,280-line general language-server test driver into independent
  diagnostics/recovery, completion/inlay, native-editor, and project-index
  executables between 290 and 474 lines; failures now identify the owning LSP
  capability instead of stopping one monolithic sequence
- moved lint behavior out of the 1,337-line navigation driver and into the
  diagnostics owner, then split navigation into definition/hover,
  symbol-reference identity, and module/native-reference identity executables;
  all four resulting owners are between 402 and 487 lines
- split the 1,134-line AST frontend driver into semantic-token, parser
  recovery, statement/unsupported-syntax, and declaration/match/editor-query
  executables between 259 and 425 lines
- split the 893-line mixed frontend driver into syntax/fixture,
  diagnostics/formatting, and semantics/codegen executables between 229 and
  426 lines; its full manifest case now lives in the 493-line project-config
  owner and reuses that suite's fixture writer
- split the 856-line module driver into module-loader/import-identity,
  project-index graph/invalidation, and generated-module-artifact executables
  between 255 and 392 lines; only the project-index owner retains the
  repository-root fixture definition
- the default CTest inventory is now 46 targets, and adding a normal C++ test
  has one registration point instead of four synchronized edits

Validation: complete fast suite, canonical fixture execution, negative tests,
site checks, and relevant packaging/build probes.

The latest ownership-cleanup milestone passes all 46 fast test executables,
LSP smoke, invalid-edit recovery, incremental synchronization, and the LSP
matrix in 51.55 seconds with 622,488 KiB peak RSS. Representative frontend and
macro targets also build under strict warnings with `-Werror`. `raymarch-dd`,
`dudu-webserver`, every `duduplayground` native target, and the complete
`dudu-datascience` target set also build with the cleaned compiler.

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
