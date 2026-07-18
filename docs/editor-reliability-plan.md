# Editor Reliability Plan

This is the active plan for making Dudu's editor support dependable enough for
normal development. It refines [Editor Intelligence](editor-intelligence-plan.md)
and [Language Server](language-server-plan.md) around the order users experience
failures:

1. the editor must not break
2. the editor must not report wrong information
3. the editor must remain responsive
4. richer information comes after those guarantees

Broken and confidently wrong behavior are both release-blocking. When exact
semantic information is unavailable, returning no result with a useful
diagnostic is better than guessing.

## Engineering Rules

- Use the compiler lexer, parser, AST, symbol identities, semantic facts, module
  graph, and native metadata. Do not create parallel regex or source-string
  semantics in the LSP.
- Keep parser recovery independent from successful semantic analysis. One bad
  declaration must not erase highlighting and navigation for the rest of a
  document or workspace.
- Associate every asynchronous result with a document revision. Never publish
  stale diagnostics, semantic tokens, inlay hints, or indexes.
- Fail capabilities independently. A native-header scan failure must not remove
  Dudu diagnostics or local-symbol navigation.
- Be honest about incomplete C/C++ metadata. Preserve ambiguity and explain it
  instead of choosing an arbitrary declaration.
- Add a deterministic fixture for every fixed failure. Include invalid and
  partially typed source, not only compiling programs.
- Measure before optimizing. Report cold and warm latency separately.
- Keep heavyweight native-header fixtures explicit and outside the default fast
  editor loop.

## 1. Build The Adversarial Recovery Matrix

Create deterministic LSP JSON-RPC fixtures that edit valid programs into each
important invalid intermediate state and then repair them. Cover:

- incomplete identifiers, member paths, imports, calls, generic arguments, and
  indexing expressions
- missing delimiters, colons, indentation, operands, return types, and bodies
- malformed classes, enums, payload variants, functions, decorators, matches,
  loops, and exception blocks
- unresolved names and types
- duplicate or temporarily conflicting declarations
- malformed native imports and unavailable headers
- invalid macro attributes and failed macro expansion
- edits that change module exports while dependent documents remain open
- rapid revisions where a slow result finishes after a newer edit
- closing, reopening, renaming, and deleting documents during background work

For every state, assert the exact capabilities that remain available:

- lexical and semantic highlighting outside the damaged range
- parser and semantic diagnostics with useful source ranges
- hover, definition, references, rename, completion, signature help, inlay
  hints, document symbols, and formatting where structurally possible
- recovery after the source becomes valid without restarting the server or
  reloading VS Code

### Gate

The matrix reproduces known failure classes, runs without timing races, and
records capability-specific expectations for both the damaged and repaired
states.

## 2. Eliminate Breakage And Stale State

Use the matrix to remove crashes, hangs, capability dropouts, and manual reload
requirements.

- publish parser diagnostics immediately, independently of sema
- preserve the last valid project index while a new revision is incomplete
- produce partial semantic facts for unaffected declarations and bodies
- isolate semantic failures by declaration and module
- cancel or discard obsolete analysis by revision
- refresh semantic tokens, inlay hints, diagnostics, and code lenses after
  background native or macro metadata arrives
- keep document overlays separate from files on disk
- recover cleanly after failed native scans, missing build tools, and failed
  macro workers
- bound queues and background workers so repeated saves cannot wedge the server
- make shutdown and workspace-folder changes deterministic

TextMate highlighting is a final visual safety net, not a substitute for
recovering semantic tokens. Invalid source should retain all semantic
information the compiler can still prove.

### Gate

No matrix case crashes, hangs, drops unrelated capabilities, publishes stale
results, or requires `Reload Window`, rebuilding from a terminal, or reopening
the file to recover.

## 3. Build The Semantic Correctness Matrix

Create positive and negative fixtures for every supported symbol and query
class. Expected results must use stable symbol identity rather than spelling.

Cover definitions, references, rename, hover, completion, signature help,
semantic tokens, inlay hints, and diagnostics for:

- locals, parameters, loop and match bindings, receivers, fields, and methods
- functions, overloads, constants, classes, enums, variants, aliases, modules,
  imports, and re-exports
- generic type and value parameters, associated aliases, shaped arrays, slices,
  index operators, and inferred types
- inheritance, overrides, abstract methods, static members, and base members
- decorators and macro-generated declarations
- C functions, C++ namespaces, classes, templates, overloads, aliases, enum
  values, fields, methods, macros, generated declarations, and source files
- identical spellings in different scopes, modules, namespaces, and overload
  sets
- aliases whose visible Dudu identity differs from their native identity

Negative fixtures must verify that ambiguous, unresolved, inaccessible, or
scanner-incomplete symbols do not produce plausible but incorrect answers.

### Gate

Every public language construct and native import category has deterministic
identity-based assertions for the relevant editor capabilities.

## 4. Eliminate Wrong Information

Fix the failures exposed by the correctness matrix.

- diagnostics identify the correct token, rule, inferred and expected types,
  related declaration, and actionable correction
- hover and inlay types use the same semantic result as compilation
- each component of `a.b.c` resolves independently
- import module/path segments, imported names, and aliases navigate to the
  appropriate file or declaration
- references and rename distinguish shadowed names, overloads, aliases, fields,
  and equal spellings in different modules
- signature help chooses the correct overload and active argument
- generated declarations retain a navigable source/provenance chain
- quick fixes use AST/token ranges and produce canonical compiling Dudu
- native ambiguity reports candidate declarations instead of guessing
- formatter output is stable, idempotent, and accepted by the parser

### Gate

The correctness matrix is green, all quick-fix outputs compile where promised,
formatting is idempotent, and no capability silently substitutes spelling-based
results for symbol identity.

## 5. Meet The Latency Budgets

Measure the complete editor path after correctness is stable. Use fresh server
processes for cold measurements and multiple requests for warm measurements.

Record median, p95, peak RSS, project, toolchain, cache state, and phase timings
for:

- workspace initialization and first parser diagnostics
- full semantic diagnostics
- first and warm hover, definition, references, rename, completion, signature
  help, semantic tokens, inlay hints, and formatting
- first native-aware query and cached native queries
- invalid-edit analysis and repaired-source recovery
- small fixtures, `raymarch-dd`, `dudu-webserver`, macro projects, and a
  template-heavy native project

Initial targets remain:

| Operation | Target |
| --- | ---: |
| Cold workspace usable | under 1 s |
| Warm hover or definition | under 50 ms |
| Warm references or semantic tokens | under 100 ms |
| Parser diagnostics after edit | under 50 ms |
| Repaired-source recovery | under 100 ms for Dudu-only edits |

Optimize architecture before adding caches: avoid whole-project reanalysis,
copying symbol graphs, repeated module loads, duplicate native scans, and
unnecessary process launches. Every cache needs a deterministic identity,
invalidation fixture, and observable hit/miss state.

### Gate

Tracked cases meet their budgets or identify a bounded external tool cost with
phase evidence. Optimization does not weaken diagnostics, recovery, generated
code, or semantic correctness.

Status: complete on the July 18, 2026 reference matrix. Five-sample Release
measurements cover a small generic project, a declaration-macro project, a
template-heavy native project, `raymarch-dd`, and `dudu-webserver`. All p95
budgets pass: workspace usability is at most 8.6 ms, parser diagnostics after
malformed edits 0.6 ms, repaired-source recovery 7.1 ms, warm definition 14.7
ms, warm references 82.6 ms, and warm semantic tokens 3.2 ms. An empty native
cache makes the first template-heavy Clang query take 1.63 seconds without
delaying the 7.6 ms parser publication. Full methods, cache state, hardware,
RSS, and reproduction commands are recorded in
[Performance](performance.md#editor-latency).

## 6. Add Rich Information And Native Documentation

After reliability, correctness, and latency are green, make the editor explain
both Dudu and imported APIs without forcing users to search headers manually.

### Dudu Documentation

- render Python-shaped docstrings and declaration comments as Markdown
- show complete signatures, inferred types, generic parameters and defaults,
  shaped extents, class/enum previews, layouts, and declaration provenance
- include parameter and return documentation in hover and signature help
- document language-provided decorators, operators, builtins, collection
  methods, indexing forms, and deliberate restrictions
- preserve documentation and navigation for macro-generated declarations

### C And C++ Passthrough

For imported headers, preserve and present every useful fact Clang provides:

- resolved header, include kind, package/include root, source location,
  namespace, visible Dudu name, and native identity
- Dudu-facing signature beside the original C/C++ declaration
- Doxygen and ordinary declaration comments, parameter and return docs,
  deprecation annotations, defaults, qualifiers, and template parameters
- overload sets with the selected overload identified
- type declaration previews, fields, bases, aliases, enum values, size,
  alignment, and source navigation
- macro parameters, documentation, expansion category, and definition location
- normal treatment of generated APIs such as protobuf declarations
- import and re-export provenance across Dudu modules

Use a consistent hover order:

1. Dudu-facing declaration
2. native declaration or identity when applicable
3. documentation
4. type layout and structural preview
5. definition location and import provenance

Do not invent prose when a native header contains no documentation. A precise
signature, declaration preview, identity, and source location are still useful.
Cache parsed documentation with the same header identity and invalidation rules
as native semantic metadata.

### Editor Presentation

- syntax-highlight Dudu and native declaration fences in hover
- make hover definition links and inlay-hint label parts navigable
- provide rich signature help for generic, native, and index operations
- show concise defaults with optional expansion for large declarations
- keep code lenses opt-in where they add visual noise
- expose generated C++ for a selected Dudu declaration or expression through a
  dedicated command without making generated text part of semantic analysis

### Gate

Representative Dudu, C, C++, standard-library, template-heavy, macro, and
generated-library fixtures show useful hover/signature documentation and
correct navigation. Missing native documentation degrades honestly to
structured declaration information.

Status: complete on July 18, 2026. The deterministic rich-documentation fixture
checks Dudu docstrings, parameter and return documentation, class and enum
previews, layouts, aliases, ordinary C and C++, template defaults and overload
selection, standard-library declarations, macros, and generated schema APIs.
Native comments, declarations, deprecation metadata, layout, identity, source
location, and import provenance survive the on-disk scan cache and a server
restart. VS Code exposes the same server data and provides a dedicated command
for opening generated C++ without feeding generated text back into semantic
analysis. `scripts/test_lsp_rich_docs.sh` is the executable gate.

## Final Completion Gate

- all adversarial recovery fixtures pass without reloads or restarts
- all semantic correctness fixtures pass by symbol identity
- latency budgets are measured and met on the documented machine and corpora
- native documentation passthrough works for ordinary C, C++, standard-library,
  template-heavy, macro, and generated APIs
- formatter and quick-fix outputs are stable and tested
- VS Code validation passes in `raymarch-dd` and `dudu-webserver`
- the implementation contains no editor-only semantic parser, library-name
  special case, or source-string fallback

Status: complete on July 18, 2026. The adversarial 21-state recovery suite,
incremental synchronization suite, semantic matrix, rich-documentation gate,
55 native test targets, formatter/site guards, and the `raymarch-dd`,
`dudu-webserver`, and `dudu-datascience` dogfood checks pass from the current
tree. The measured Release editor matrix meets the stated latency budgets.
