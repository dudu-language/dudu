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
open/change/save events, displays LSP diagnostics, uses the LSP formatting
provider for `Dudu: Format Current File` and format-on-save, and shows a status
bar item with LSP process state, configured `duc` path, and native-header
diagnostic state. The status tooltip also shows the current `dudu.toml`
`[target]` kind/mode when available.

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
diagnostics, and returns full-document formatting edits. The implementation
uses the existing parser, semantic checker, formatter, and native-header scanner
in-process.

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
open buffers taking precedence. A fuller import-graph project index remains.

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
included in completion. Deeper block-sensitive local scope completion and richer
snippet coverage remain.

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
uses the real header location when Clang provides it. Macro metadata,
including object-like/function-like macro hover and completion, is also exposed
for scanned native headers. Initial native C++ member completion is implemented
for locals annotated with scanned native class types or aliases. Deeper overload
display and broader real-library coverage remain.

### Milestone 5: Refactors And Code Actions

- safe rename for Dudu symbols
- organize imports
- add missing import when unambiguous
- quick fix for missing include path or missing package config entry when the
  project config suggests the fix
- optional warnings and lint code actions

Status: conservative `textDocument/rename` is implemented for Dudu symbols in
currently open documents and wired into the VS Code extension. It validates the
replacement as an identifier and refuses dotted/native symbols. Initial
`textDocument/codeAction` support is implemented with a format-document source
action wired into VS Code. Organize-imports code actions are implemented for the
leading import block and return a WorkspaceEdit through the VS Code adapter.
Missing-import quick fixes are implemented for unambiguous Dudu workspace
symbols and insert a `from module import symbol` edit. Native config quick fixes
and project-wide rename beyond open documents remain.

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
imports, native macro hover/completion, and strict missing-header diagnostics.
It also covers workspace symbols and references from an unopened sibling `.dd`
file, plus Dudu-native and native C++ member completion.
Imported Dudu module completion is covered with an unopened sibling module.
Completion resolve, snippets, and typed local completions are covered.
Format and organize-imports code actions are covered.
Missing-import quick fixes are covered using an unopened workspace file.

## Non-Goals

- implementing a separate parser in TypeScript
- replacing Clang for C/C++ header understanding
- promising perfect macro expansion navigation for every C/C++ macro shape
- jumping to generated C++ as the primary definition target when a real Dudu or
  native source location exists
