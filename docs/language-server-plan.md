# Dudu Language Server Plan

Dudu needs a real language server. Syntax highlighting alone is not enough for
a systems language that promises full C/C++ interop.

The language server should make `.dd` files feel native in VS Code and other
LSP-capable editors:

- red squiggles for parse, semantic, native-header, and build-configuration
  diagnostics
- warnings for unused locals, unreachable code, shadowing, suspicious casts,
  and native interop hazards
- go to definition for Dudu functions, classes, fields, constants, modules, and
  imports
- find all references for Dudu declarations
- rename for Dudu declarations when it can be done safely
- hover for inferred types, function signatures, doc comments, and native
  header symbols
- completion for local bindings, module symbols, class members, imported native
  symbols, macros, enum values, and snippets for common forms
- signature help for Dudu and native C/C++ calls
- document symbols and workspace symbols
- code actions for simple fixes, such as adding missing imports or formatting
  the file
- formatting via the existing formatter

## Critical Editor Reliability Blocker

Editor requests must always complete or fail with a visible diagnostic. They
must not spin indefinitely.

Observed failures from the `raymarch-dd` sanity workspace:

- go-to-definition spins in VS Code
- find-all-references spins in VS Code
- hover over Dudu types stays at `Loading...`

These are correctness bugs in the language-server request path, project root
detection, module indexing, native-header scan scheduling, or VS Code adapter.
They must be covered by LSP JSON-RPC tests that assert every request returns a
response, even when the symbol cannot be resolved. The server should log and
surface project-index/native-scan failures instead of leaving the client
waiting.

Status: request handlers now return JSON-RPC error responses when a request
throws after an ID is known. The LSP smoke suite covers a bad-project-config
definition request that previously could leave the editor waiting.
Cursor-position parsing for definition, references, hover, completion, and
other position-based requests now rejects missing or non-numeric
`position.line` / `position.character` with a JSON-RPC error instead of
silently defaulting to the start of the file.

## JSON-RPC Request Validation

Required LSP protocol fields must fail loudly when they are missing or have the
wrong type. The server should return a JSON-RPC error for malformed requests
instead of silently substituting default values. Silent defaults can send the
cursor to line zero, query the wrong document, or hide client/protocol bugs.

Examples of required fields:

- request `method`
- `textDocument.uri`
- `position.line`
- `position.character`

Optional protocol fields may still use explicit defaults when the LSP spec
defines them as optional. Helper names should make that distinction obvious:
prefer required accessors for required fields and optional/default accessors
only for optional fields.

Status: required integer fields use explicit required accessors; optional
integer fields use `optional_*` helpers with named default values. A smoke
fixture covers a malformed definition request with a missing position.

## Native Header Navigation

The language server should use the same Clang-backed native header awareness as
`duc check` and `duc emit`.

Native interop support should include:

- jump from `rl.Vector2` to the actual `Vector2` declaration in `raylib.h`
- jump from `sqlite.sqlite3_open` to `sqlite3_open` in `sqlite3.h`
- jump from imported C++ methods to class declarations and method declarations
- hover native function signatures using Dudu-shaped types and the original C++
  spelling when useful
- complete imported C/C++ symbols after `alias.`
- complete direct imported C globals without `alias.`
- show macro arity and whether a macro is object-like or function-like
- preserve the native source location returned by Clang for every scanned
  native type, value, function, class, method, enum, and macro

Native jumps should prefer real header locations. If a symbol is synthesized or
only available through macro expansion, the server should say that clearly in
hover/diagnostics instead of jumping to generated C++.

Status: native type-definition, hover, completion, and document-symbol detail
now index scanned native classes by Dudu binding name and `NativeSymbolId`.
Imported type aliases use identity when the scanner provides enough metadata
and otherwise follow their structured alias target `TypeRef` head to the real
class declaration. Broader native references and semantic token paths still
need to move from name-set lookup toward the same canonical identity model.

## Architecture

Add `duc lsp` as the editor-facing server process.

The server should be built on the existing compiler library:

- lexer/parser for document structure
- module loader for project imports
- semantic checker for diagnostics
- native header scanner for C/C++ symbols and source locations
- formatter for formatting requests
- project driver config for include paths, defines, package config libraries,
  target mode, and build flags

The important implementation constraint is that editor requests must not shell
out to `duc check` for every keystroke. The compiler pieces need reusable APIs
that operate on in-memory source text and cached project state.

Recommended internal pieces:

- `DocumentStore`: open documents, versions, dirty text, and file paths
- `ProjectIndex`: parsed modules, symbol tables, reverse references, and import
  graph
- `NativeIndex`: Clang scan results keyed by header path, compiler flags, and
  modification stamp
- `DiagnosticsEngine`: incremental parse/semantic/native diagnostics
- `DefinitionEngine`: Dudu and native definition lookup
- `ReferenceEngine`: reference search across loaded project files
- `CompletionEngine`: local, module, member, native, macro, and snippet
  completions

## Incrementality

The first useful version can be conservative:

- reparse the edited document on change
- rebuild the project symbol index on save
- rescan native headers only when imports, include paths, defines, package
  config packages, or header mtimes change

Later versions should incrementally update the import graph, affected semantic
scopes, and reference index.

The server must avoid repeating expensive scans in a tight loop. Native header
scans should use the existing cache and should debounce document changes before
requesting Clang work.

Status: native header scanning uses the existing in-memory and raw on-disk
header cache, and the VS Code client debounces full-document change
notifications before requesting fresh diagnostics.

## Diagnostics

Diagnostics should be grouped by source:

- `dudu/parser`
- `dudu/sema`
- `dudu/native-header`
- `dudu/build-config`
- `dudu/lint`

Native header failures should include actionable messages:

- missing `clang++`
- missing header
- missing include path
- missing `pkg-config` package
- bad define or target flag
- native parse failure with relevant Clang output

Warnings should be opt-in at first if they risk noise. Errors should match
`duc check` behavior.

## VS Code Integration

The existing VS Code extension should start `duc lsp` for `.dd` files.

The extension should provide:

- `.dd` language registration
- TextMate highlighting as the baseline
- LSP client wiring
- format on save support
- command palette actions for `dudu check`, `dudu build`, `dudu run`, and
  `dudu test`
- status item showing project target, compiler path, and whether native header
  awareness is active

The extension should not duplicate compiler logic in TypeScript. It should
delegate intelligence to `duc lsp`.

Status: the local VS Code extension starts `duc lsp`, forwards document
open/change/save events, debounces rapid change notifications, displays LSP
diagnostics, uses the LSP formatting provider for `Dudu: Format Current File`
and format-on-save, and shows a status bar item with LSP process state,
configured `duc` path, and native-header diagnostic state. The status tooltip
also shows the current `dudu.toml` `[target]` kind/mode when available. Command
palette actions are registered for formatting, checking the current file,
building the project, running the current file, and running project tests.

## Protocol Features By Milestone

### Milestone 1: Diagnostics And Formatting

- `initialize`
- `textDocument/didOpen`
- `textDocument/didChange`
- `textDocument/didSave`
- `textDocument/publishDiagnostics`
- `textDocument/formatting`

This milestone proves the server process, document sync, parse errors, semantic
errors, and formatter integration.

Status: initial `duc lsp` support is implemented. It speaks JSON-RPC over
stdio, handles full-document sync, publishes parse/semantic/native-header
and build-configuration diagnostics, including invalid project config and
missing `pkg-config` package diagnostics, and returns full-document formatting edits.
The implementation uses the existing parser, semantic checker, formatter, and
native-header scanner in-process.
The formatter trims trailing whitespace, caps repeated blank lines, sorts the
leading import block, and normalizes leading tab indentation to four spaces.

### Milestone 2: Dudu Navigation

- `textDocument/definition`
- `textDocument/references`
- `textDocument/hover`
- `textDocument/documentSymbol`
- `workspace/symbol`

This milestone covers Dudu-only code before native header navigation.

Status: initial Dudu-only `definition`, `hover`, `documentSymbol`,
`textDocument/references`, and `workspace/symbol` support is implemented for
open documents and wired into the VS Code extension. Workspace symbols and
references also scan unopened `.dd` files under the active project roots, with
open buffers taking precedence. Go-to-definition resolves Dudu module import
aliases to imported module files and resolves `from module import symbol`
bindings to the imported declaration. The workspace index now follows imported
Dudu modules before broad workspace scanning, so direct import dependencies are
available even when they live under directories excluded from recursive scans.
Hover includes Dudu declarations plus visible typed locals and simple inferred
locals. Dudu declaration hover also includes contiguous source comments
immediately above the declaration. Hover now resolves Dudu symbols reached
through imported module aliases, so `module.symbol` shows the imported
declaration signature instead of falling back to an empty hover. Unaliased
nested module imports such as `import vendor.helper` now also resolve through
the full dotted path for hover, go-to-definition, and member completion.

Initial full-document semantic tokens are also implemented for Dudu AST nodes,
covering declarations, parameters, fields, locals, types, literals, calls, and
member expressions. Semantic tokens merge scanned native header symbols as a
classification layer, so native C/C++ references in Dudu code can carry the
`native` modifier without emitting header-file token ranges.

### Milestone 3: Completion And Signature Help

- `textDocument/completion`
- `completionItem/resolve`
- `textDocument/signatureHelp`

This milestone should include local symbols, module symbols, class members, and
basic native symbols.

Status: initial `textDocument/completion` support is implemented for Dudu
keywords, built-in types, and open-document Dudu symbols, and initial
`textDocument/signatureHelp` support is implemented for open-document Dudu
functions. VS Code now uses LSP completions and signature help. Initial
member-aware completion is implemented for Dudu-native class locals with simple
type annotations, and imported Dudu module aliases complete symbols from the
imported file. Completion resolve is implemented, common-form snippets are
returned with snippet insert text, and typed locals in the current document are
included in completion. Local completion now filters out declarations from
deeper indentation levels when completing in an outer block. Richer snippet
coverage is implemented for common function, type, control-flow, import, and
exception forms.

### Milestone 4: Native Header Navigation

- definition/hover/completion/signature help for imported C/C++ symbols
- macro metadata in hover/completion
- jump through aliases like `rl.Vector2` to `Vector2` in `raylib.h`
- jump through direct imports like `Vector2` to `raylib.h`
- jump through C++ namespaces/classes and member methods

This milestone is the one that makes Dudu feel serious for interop.

Status: initial native C/C++ symbols from local fixture headers are now scanned
into LSP symbols. Completion, hover, signature help, and definition can resolve
aliased imported functions such as `dudu_native.dudu_native_add`, and definition
uses the real header location when Clang provides it. Direct native imports
without aliases are covered for completion, signature help, and definition.
Macro metadata,
including object-like/function-like macro hover and completion, is also exposed
for scanned native headers. Initial native C++ member completion is implemented
for locals annotated with scanned native class types or aliases, and
go-to-definition for those native C++ members jumps to the scanned header
location. Signature help returns all scanned overloads for matching native
functions. Optional LSP probes cover real `sqlite3` and `raylib` headers through
`pkg-config`, along with real `SDL3` and `GLFW` headers when those packages are
available; broader real-library coverage can continue in that optional suite.

### Milestone 5: Refactors And Code Actions

- safe rename for Dudu symbols
- organize imports
- add missing import when unambiguous
- quick fix for missing include path or missing package config entry when the
  project config suggests the fix
- optional warnings and lint code actions

Status: conservative `textDocument/rename` is implemented for Dudu declaration
symbols across open documents and unopened `.dd` workspace files, and is wired
into the VS Code extension. It validates the replacement as an identifier,
refuses dotted/native symbols, and skips workspace files that declare their own
same-named Dudu symbol so declaration rename does not edit obvious unrelated
symbols. It allows use-site-triggered rename edits only inside the current
document for an unqualified call expression that resolves to one renameable
Dudu declaration in that document with no visible local type binding shadowing
it. Other use-site rename requests stay rejected until the LSP can prove symbol
identity strongly enough to avoid editing unrelated same-named locals. Initial
`textDocument/codeAction` support is implemented with a format-document source
action wired into VS Code. Organize-imports code actions are implemented for the
leading import block and return a WorkspaceEdit through the VS Code adapter.
Missing-import quick fixes are implemented for unambiguous Dudu workspace
symbols and insert a `from module import symbol` edit. Native config quick fixes
are implemented for known missing native headers that map cleanly to
`pkg-config` packages such as `raylib`, `sqlite3`, `sdl3`, `glfw3`, and
`vulkan`. Initial lint diagnostics warn on unreachable statements after
`return`, simple unused typed locals, and local bindings that shadow visible
outer bindings. They also warn on known numeric narrowing casts and raw
`cpp(...)` escape hatches. Unreachable and unused-local diagnostics include
quick fixes that remove the flagged line.

## Tests

Add language-server tests that drive the server with LSP JSON-RPC messages.

Required fixtures:

- single-file diagnostics
- multi-file imports
- direct native import: `import c "raylib.h"` and `Vector2`
- aliased native import: `import c "raylib.h" as rl` and `rl.Vector2`
- direct sqlite calls through `sqlite3.h`
- C++ namespace import and C++ class/member lookup
- macro hover/completion
- missing header diagnostics
- missing package config diagnostics
- reference search across modules

These tests should not require every optional native dependency for the core
suite. Use small local fixture headers for core LSP behavior, and keep real
library probes under optional native tests.

Status: the smoke suite now drives LSP JSON-RPC for single-file diagnostics,
formatting, Dudu symbols, references, rename, workspace symbols, native fixture
imports, native macro hover/completion, strict missing-header diagnostics, and
build-configuration diagnostics from a broken `dudu.toml`.
It also covers workspace symbols and references from an unopened sibling `.dd`
file, imported modules from a skipped `vendor` directory, plus Dudu-native and
native C++ member completion.
Declaration-anchored references skip unrelated same-named Dudu declarations,
while imported module/member references still search the workspace.
Imported Dudu module completion is covered with an unopened sibling module.
Completion resolve, snippets, and typed local completions are covered.
Common-form snippets for functions, classes, control flow, imports, enums, and
exception handlers are covered.
Hover for typed locals and simple inferred locals is covered.
Hover for Dudu declaration doc comments is covered.
Hover for Dudu symbols imported through module aliases is covered.
Format and organize-imports code actions are covered. Formatter and LSP
organize-imports behavior share the same structured leading-import block
helper.
Missing-import quick fixes are covered using an unopened workspace file, with
an ambiguity guard that suppresses the fix when more than one workspace module
exports the missing name.
Native config quick fixes are covered with a fixture `dudu.toml` edit for a
known missing native header package.
Missing `pkg-config` package diagnostics are covered with a fixture
`dudu.toml`.
Direct native imports without aliases are covered for completion, signature
help, and definition with a local fixture header.
Native enum values from scanned headers are covered in symbol and completion
results.
Workspace rename is covered across an open definition file and an unopened
sibling use file, plus a guard that an open unrelated same-named declaration is
not edited.
Current-document unqualified call-site rename is covered for calls that resolve
to one declaration, including a guard that an unrelated same-named symbol in
another open document is not edited; ambiguous use-sites remain intentionally
rejected.
Unreachable-statement lint diagnostics and remove-line quick fixes are covered.
Unused-local lint diagnostics and remove-line quick fixes are covered.
Local-shadowing lint diagnostics are covered.
Suspicious narrowing-cast and raw native escape-hatch lint diagnostics are
covered.
Go-to-definition for Dudu module import aliases is covered with an unopened
module file. Go-to-definition for `from module import symbol` aliases is
covered with an unopened module file. Hover, definition, and completion for
unaliased nested module imports are covered with a vendored module fixture.
Go-to-definition for native C++ member methods is covered with a local fixture
header. Go-to-definition for imported native type annotations also follows a
structured native type alias to its scanned class declaration when that target
is available.
Go-to-definition for imported C/C++ headers respects manifest-relative
`[include] paths`, matching the project-driver path contract.
Native overloaded function signature help is covered with a local fixture
header.
Local completion scope filtering is covered so deeper-block locals do not leak
into outer-block completions.
Optional LSP probes cover real `sqlite3` and `raylib` headers through
`pkg-config`, plus `SDL3` and `GLFW` when installed, including diagnostics,
completion, signature help, and definition. They are part of `scripts/test_full.sh`
so the broad developer sweep exercises real-header editor behavior as well as
native compile/run probes.

## Non-Goals

- implementing a separate parser in TypeScript
- replacing Clang for C/C++ header understanding
- promising perfect macro expansion navigation for every C/C++ macro shape
- jumping to generated C++ as the primary definition target when a real Dudu or
  native source location exists
