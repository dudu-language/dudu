# Dudu Language Server Plan

Dudu needs a real language server. Syntax highlighting alone is not enough for
a systems language that promises full C/C++ interop.

Detailed editor-quality goals live in
[Editor Intelligence Plan](editor-intelligence-plan.md). That plan defines the
Rust-Analyzer/clangd-like target for semantic highlighting, local/native
go-to-definition, rich hover, doc comments, native header jumps, and VS Code
language-client integration.

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

Status: native type-definition, hover, completion, workspace-symbol detail, and
semantic token classification now index scanned native classes by Dudu binding
name and `NativeSymbolId`. Imported type aliases use identity when the scanner
provides enough metadata and otherwise follow their structured alias target
`TypeRef` head to the real class declaration. References now also handle
path-qualified member uses such as an imported native class constructor call
matching the same qualified type annotation, from either cursor position.
Frontend tests cover go-to-definition from a Dudu native type annotation to the
source location of a real scanned local C header declaration.
LSP `Symbol` entries now carry native identity keys for scanned native types,
values, functions, macros, classes, and C++ class methods when Clang metadata
provides them. That keeps identity available at the editor boundary and is the
next step toward replacing native reference matching by plain source spelling.
Native hover now shows that canonical identity key beside the Dudu-shaped
signature/type when it is available.
Find-references now uses those native identity keys to filter same-spelled
native references across workspace documents when identity metadata is
available, so two headers imported under the same alias with the same function
name are not conflated. `ProjectIndex` now builds a per-module native identity
table for native types, values, macros, functions, classes, C++ methods, and
native class fields/constants/static fields, so native reference filtering uses
indexed symbol identity instead of rebuilding LSP symbol lists per candidate
document. Single-file editor overlays are also stamped with their entry path, so
open documents that do not exist on disk still participate in indexed native
reference filtering. Dudu import aliases that happen to use native-shaped symbol
records are not treated as native references when their declaration location is
in `.dd` source.
Reference and rename workspace scans now prefer module units from the already
loaded warm `ProjectIndex` for candidate files in the same module tree before
falling back to per-document project loads, keeping repeated requests on shared
server/index state.
Definition and hover symbol lookup now prefer exact symbols, and only fall back
to suffix matching when the suffix is unambiguous. Receiver-aware member
definition and hover run before suffix fallback, so native classes with
same-named methods resolve through the expression receiver type instead of
whichever method appears first in the scanned symbol list.
Find-references keeps unresolved member expressions as dotted queries instead
of falling back to the bare member name, so unrelated same-named member calls
are not reported together. Module-qualified references include the declaration
inside the target module by searching that module with the unqualified imported
member name.
Dudu class member declaration references now use class-qualified identity for
the selected declaration and receiver-type checks for member expressions, so
`Player.hp` references do not include unrelated `Enemy.hp` declarations or
uses. Member use sites such as `player.hp` and `player.move(...)` now resolve
through the receiver's structured type to the same qualified member identity,
including when `Player` is imported from another Dudu module. Local context for
these checks is file-aware inside merged module views and treats method `self`
as the containing class before walking the method body. Enum value declaration
references use the same qualified identity shape, so `Mode.Play` does not
include another enum's `Play` variant. Member completion for enum and sum-type
names now resolves structured type aliases too, so imported `Mode.` and
`Token.` completions offer the correct variants with enum-member item kinds and
attached docs.

## Architecture

Add `dudu-lsp` as the editor-facing server process.

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

The existing VS Code extension should start `dudu-lsp` for `.dd` files.

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
delegate intelligence to `dudu-lsp`.

The current extension uses a hand-written JSON-RPC client. That was enough to
bootstrap diagnostics and several requests, but it leaves feature classes such as
semantic tokens easy to miss. The serious path is to use VS Code's
`vscode-languageclient` package so standard LSP capabilities, including semantic
tokens, are wired by normal client plumbing while `dudu-lsp` remains the owner of
compiler intelligence.

Status: the local VS Code extension currently starts `dudu-lsp`, forwards document
open/change/save events, displays LSP diagnostics, uses the LSP formatting
provider for `Dudu: Format Current File` and format-on-save, and shows a status
bar item with LSP process state, configured `dudu` / `dudu-lsp` paths, target
kind/mode, and native-header diagnostic state. Command palette actions are
registered for formatting, checking the current file, building the project,
running the current file, running project tests, and restarting the language
server. The extension now uses VS Code's standard `vscode-languageclient`
package instead of a broad hand-written JSON-RPC client, so semantic tokens and
normal LSP capabilities are wired through standard client plumbing.
The clean target is a dedicated `dudu-lsp` binary; do not preserve `duc lsp` as
an alias after the tool split lands.

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

Status: initial bootstrap LSP support is implemented. It speaks JSON-RPC over
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
Unmatched expression names, callees, and member fallbacks now carry the
advertised `unresolved` modifier while known local bindings stay unmarked.

## Doc Comments And Docstrings

Hover documentation should feel Python-shaped and useful for real libraries.
Dudu should support both lightweight comment blocks and full docstrings:

```python
# Adds two signed integers.
# This is good for short API notes.
def add(a: i32, b: i32) -> i32:
    return a + b


def connect(host: str, port: i32) -> Result[Socket, Error]:
    '''
    Open a TCP connection.

    Args:
        host: DNS name or address.
        port: TCP port.

    Returns:
        A connected socket or an error value.
    '''
    ...
```

The official documentation syntax should be:

- contiguous `#` comments immediately before a declaration
- a leading triple-single-quoted string body, `''' ... '''`, as the first
  statement inside functions, classes, methods, enums, and modules for larger
  API docs, matching Python's docstring shape
- markdown-ish plain text in LSP hovers, completion resolve, and signature help

`#` comment docs are best for short summaries. Triple-quoted docstrings are for
multi-paragraph library documentation, parameter descriptions, examples, and
notes. Docstrings are documentation, not runtime string expressions. Unlike
Rust/Zig-style declaration docs, large Dudu docstrings live inside the thing
they document because the broader language goal is Python-shaped source.

Current support is partial but real: contiguous `#` comments immediately above
Dudu declarations attach to parsed declaration AST nodes, and first-body
triple-single-quoted docstrings attach to modules, functions, methods, classes,
and enums. Docstring statements are removed from the executable statement list,
so they are documentation rather than runtime string expressions. Hover shows
attached docs for same-file declarations, imported Dudu module symbols, and
typed member fields such as `player.hp`, including module docs when hovering a
module import alias.
Completion items now carry those AST docs for local, imported, and Dudu member
symbols, and `completionItem/resolve` preserves that documentation payload
instead of reconstructing it from display text. Signature help also shows those
docs for Dudu functions visible through the ProjectIndex. Type aliases are now
first-class LSP symbols, so hover and definition can use their attached docs the
same way classes and other type declarations do. Direct tests cover same-file
alias and constant definition from use sites, and the JSON-RPC matrix covers
imported alias definition back to the source module. Misplaced
module/class/enum/function docstrings now produce explicit parser diagnostics
instead of generic syntax failures or inert string statements. Document symbols
now use proper LSP `DocumentSymbol` objects and put a short AST doc summary
into `detail` when a symbol has docs. The JSON-RPC LSP matrix covers class,
field, typed field hover, method, imported completion, signature help, import
hover, and document-symbol doc propagation, including docstring-backed module,
class, enum, enum-value, method, and function docs, plus leading-comment docs
for constants and aliases. Hover for selective imports preserves suffix
identity, so `Mode.Play` resolves through the imported `Mode` symbol to the enum
variant docs.

Semantic tokens are also ProjectIndex-aware for Dudu imports. Module aliases
such as `math`, selective imported classes such as `Player`, imported functions
such as `math.mix`, and imported constants such as `math.MAGIC` now color from
symbol identity instead of local syntax shape. The JSON-RPC LSP matrix decodes
the returned token stream and asserts those imported token kinds.

Remaining work:

1. Decide whether one-line declarations such as fields, constants, and aliases
   need a larger-doc form beyond leading `#` comments.
2. Add direct native C/C++ documentation fixtures once scanner support exists.
3. Extend formatting rules and formatter behavior for docstrings, including
   indentation trimming and blank-line preservation.
4. Extend parser diagnostics for malformed docstrings if new malformed cases
   appear beyond unterminated triple strings and misplaced docstrings.
5. Add larger-doc LSP fixtures for fields/constants/aliases if a larger-doc
   syntax is added.
6. Extend native C/C++ documentation fixtures beyond native functions and
   namespaces when the scanner exposes useful docs for more declaration kinds.

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
exception forms. Completion items for Dudu symbols preserve AST-backed
documentation comments across current-document symbols, selective imports,
module completions, and member completions, and completion resolve keeps that
documentation instead of replacing it with a generic detail string. Signature
help includes AST documentation for Dudu functions reached through the same
visible symbol index.

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
Native function hover/detail includes the lowered Dudu signature plus a compact
scanner-derived native signature suffix such as `native i32(i32, i32)` when the
scanner has non-synthesized return/parameter spelling.
Doc-commented native enum constants are covered as native values in the
JSON-RPC matrix: completion and hover show scanned docs, definition jumps to
the header declaration, references include the value use site, and semantic
tokens classify the value as readonly/native.
The native scanner also parses Clang `FullComment`/`TextComment` nodes and
preserves header comments on native declarations through the scan cache, so
hover/completion/signature-help documentation can include real C/C++ declaration
docs when available. Direct tests cover native C++ class docs and alias hover
falling back to the resolved native class docs. The JSON-RPC LSP matrix covers
native function header docs through completion and signature help, native C++
class docs through hover, plus native C++ member field/method docs through
member completion, receiver-aware field go-to-definition, and receiver-aware
method signature help. The matrix also covers native C++ member method
go-to-definition and find-references with a same-named method on an unrelated
native receiver type filtered out. Native C++ constructor calls are covered as
well: signature help surfaces scanned constructor docs/signatures, and
go-to-definition on a constructor call targets the scanned constructor
declaration instead of the containing class when that location is available.
Hover uses the same constructor target, so constructor-call hover reports the
constructor signature/docs rather than the class declaration docs.
Macro metadata,
including object-like/function-like macro hover and completion, is also exposed
for scanned native headers. Local imported headers now attach macro symbols to
the matching `#define` line when the scanner can read the header text, so
go-to-definition on a native macro jumps to the header instead of the Dudu import
alias. Leading `//` / `///` / block comments immediately above those local
`#define`s are preserved as native macro docs. The JSON-RPC matrix covers native
macro completion docs, hover docs, signature-help docs, definition, references,
and semantic-token classification.
Initial native C++ member completion is implemented
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
it. Module-qualified and selective-import Dudu use sites now also rename through
the same target identity used by find-references: a rename from `math.mix` edits
the imported declaration and matching `math.mix` uses while leaving unrelated
`other_math.mix` symbols untouched, and a rename from `MAX_HP` edits the source
constant declaration/import/use sites without touching `other_entities.MAX_HP`.
`textDocument/prepareRename` is implemented and advertised through
`renameProvider.prepareProvider`. It returns the exact token range and
placeholder for renameable Dudu symbols and rejects native symbols instead of
opening a misleading editor rename prompt.
Other use-site rename requests stay rejected until the LSP can prove symbol
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

The validation suite should include curated mini-workspaces for each class of
editor action, not only `raymarch-dd` and `dudu-webserver`. Required fixture
families include module aliases, selective imports, nested module paths,
classes, instance methods, static methods, fields, constants, enums, sum types,
native generics, operators, locals, parameters, module-qualified symbols,
native headers, native functions, native types, native macros, missing imports,
rename conflict guards, formatter/code-action edits, and diagnostics. Dogfood
repos remain useful end-to-end checks, but the core LSP suite must prove each
navigation behavior in small deterministic projects that are cheap to run.

Status: the smoke suite now drives LSP JSON-RPC for single-file diagnostics,
formatting, Dudu symbols, references, rename, workspace symbols, native fixture
imports, native macro hover/definition/references/completion/signature-help
docs, strict
missing-header diagnostics, and build-configuration diagnostics from a broken
`dudu.toml`.
It also covers workspace symbols and references from an unopened sibling `.dd`
file, imported modules from a skipped `vendor` directory, plus Dudu-native and
native C++ member completion.
Declaration-anchored references skip unrelated same-named Dudu declarations,
while imported module/member references still search the workspace.
Qualified type references such as `rl.Vector2` are covered through both the AST
reference collector and the public references request path.
Aliased native function references such as `dudu_native.dudu_native_add` now
keep the full native path during reference lookup instead of collapsing to the
member name.
Imported Dudu function and constant references now keep module and
selective-import identity in the JSON-RPC matrix. The fixture includes
same-named functions/constants in unrelated modules and same-file member
expressions such as `other_entities.MAX_HP`, and those unrelated symbols are
filtered out of `math.mix` / `MAX_HP` reference results.
Class-scoped/static Dudu member paths now resolve as class member identities for
definition, hover, and references. Direct tests and the JSON-RPC matrix cover
`Counter.count`, `Counter.LIMIT`, and `Counter.bump()` with same-named members
on an unrelated `OtherCounter` filtered out. Class/static member completion and
signature help now share that member inventory too: `Counter.` completes
constants, static fields, and methods, and `Counter.bump(` surfaces the method
signature/docs in the JSON-RPC matrix. Constructor signature help now uses the
indexed class shape as well, so class calls such as `Player(` show field/init
parameters and docs. Workspace-symbol results include class/static member
symbols such as `Counter.count` and carry first-doc-line detail summaries.
Native C++ constructor signatures and go-to-definition are also covered in the
matrix with a local fixture header: `MatrixWidget(` shows the scanned
constructor docs, definition jumps to the constructor declaration, and hover on
the constructor call shows the constructor docs/signature.
Native C++ namespaces are covered in the matrix too. Scanned namespace
declarations are LSP symbols with native identities, hover and definition on
the namespace segment jump to/show the namespace declaration, completion after
the namespace path lists namespaced functions with docs, semantic tokens
classify the namespace as a native namespace token, and find-references on the
namespace segment uses native identity so same-spelled namespaces from other
headers do not leak in. Header comments on native namespace declarations are
preserved through the scan cache and shown in hover.
Dudu-owned declaration and unique-reference scope checks use Dudu-only document
symbols, so ordinary references do not trigger native header scanning unless
the selected symbol is an explicit native import.
Imported Dudu module completion is covered with an unopened sibling module.
Completion resolve, snippets, and typed local completions are covered.
`completionItem/resolve` is covered through a JSON-RPC request that asserts the
resolved item preserves label, detail, and markdown documentation.
Common-form snippets for functions, classes, control flow, imports, enums, and
exception handlers are covered.
Hover for typed locals and simple inferred locals is covered.
Hover for Dudu declaration doc comments is covered through AST-attached
declaration docs, including same-file declarations and Dudu symbols imported
through module aliases.
Format, organize-imports, missing-import, and lint-removal code actions are
covered. Formatter and LSP organize-imports behavior share the same structured
leading-import block helper. The JSON-RPC matrix asserts concrete
WorkspaceEdits for sorting imports, inserting the expected
`from module import symbol` line, and removing an unused local line. Direct unit
coverage still checks unopened workspace files and the ambiguity guard that
suppresses the missing-import fix when more than one workspace module exports
the missing name.
Native config quick fixes are covered with a fixture `dudu.toml` edit for a
known missing native header package.
Missing `pkg-config` package diagnostics are covered with a fixture
`dudu.toml`.
Direct native imports without aliases are covered for completion, signature
help, definition, and current-document references for functions with a local
fixture header.
Native enum values from scanned headers are covered in symbol and completion
results.
Doc-commented native C struct fields are covered in the JSON-RPC matrix:
receiver-typed `point.x` hover shows scanned field docs, definition jumps to
the header field, and references include typed uses without dropping to a bare
field-name search. Semantic tokens also classify the receiver-resolved field as
a native property.
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
Find-references for native C++ member fields and methods is covered in direct
tests and the JSON-RPC matrix with same-named members on unrelated receiver
types filtered out by the receiver's structured type.
Go-to-definition for imported C/C++ headers respects manifest-relative
`[include] paths`, matching the project-driver path contract.
Native overloaded function signature help is covered with a local fixture
header.
Native field navigation from a C header that only scans after the C prelude
retry is covered in the JSON-RPC matrix: `value.count` definition, hover, and
references resolve to the imported `needs_c_context.h` field identity instead
of the generated scanner wrapper or plain member spelling.
Go-to-definition for parameters, inferred assignment locals, loop bindings,
and destructured bindings is covered in both direct frontend tests and
JSON-RPC smoke fixtures.
Hover now retries local type inference against the native-enriched visible
module when plain Dudu symbols cannot infer a local, so locals assigned from
native C/C++ calls can still show useful inferred types.
Find-references for local variables is covered for same-named locals in
different functions; local reference collection stays within the selected
function/method scope when that scope owns the binding.
Payload sum-type variants such as `Token.IntLit(i64)` are covered for hover and
references, including a same-named payload variant on another enum to prove the
qualified enum-member identity is used.
Go-to-definition on Dudu operator use sites is covered in the JSON-RPC matrix:
the server selects the operator token from AST source locations, infers the
left/right operand types using the LSP module view, and jumps to the matching
`@operator(...)` method declaration. Hover and references now share that
operator identity: hovering the operator token shows the method signature/docs,
and references from the operator token include the method declaration plus typed
operator uses.
Dudu generic symbol coverage now includes generic function hover and
identity-based references, generic class hover with declaration docs, and
generic method definition/hover/references from explicit method-template call
sites. The shared LSP symbol-detail formatter includes generic parameter lists
for Dudu functions, methods, and classes so hover, completion, signature help,
and workspace symbols expose source-shaped generic signatures.
Native C++ template functions are also covered in the JSON-RPC matrix through a
local header fixture: completion, hover, go-to-definition, references,
signature help, documentation, and semantic tokens all treat explicit Dudu
template calls such as `matrix_space.identity[i32](...)` as ordinary native
symbol intelligence.
Semantic-token coverage now decodes token deltas back to source text and checks
token names, kinds, and modifiers for Dudu classes, enums, enum members, fields,
static fields, module constants, methods, parameters, implicit local bindings,
function declarations, function calls, native type/function/value/macro tokens,
unresolved symbols, numbers, and strings. The semantic-token collector also
indexes Dudu classes/enums/enum members directly, so Dudu class return types,
enum member expressions, and member calls are no longer colored as generic
types/properties/functions. The LSP matrix decodes semantic-token responses from
`dudu-lsp`, including static declarations, readonly class constants, native
namespace/function/macro tokens, and unresolved-token modifiers, so transport
coverage now matches the direct frontend assertions.
Local completion scope filtering is covered so deeper-block locals do not leak
into outer-block completions.
The server caches the expanded workspace document set across requests and
invalidates it on open-buffer changes, so repeated workspace-symbol,
references, rename, and code-action requests do not rescan the filesystem and
import graph each time. The compiler benchmark LSP probe exercises repeated
workspace-symbol and references requests so this warm workspace path is visible
outside the correctness-only smoke suite.
The language-server support layer also caches parsed/merged module views by
open-document text and native-header mode, invalidating them on document
changes and saves. Repeated hover, definition, references, completion, and
diagnostic requests for the same buffer should reuse the warm module view
instead of reparsing and remerging native headers.
Document symbols intentionally use the Dudu-owned symbol view and do not force a
native-header scan; native imported symbols remain available through explicit
native-aware requests such as hover, definition, completion, signature help,
references, and workspace symbols.
LSP native indexes now merge native headers into the source module units only,
not into both every unit and the aggregate merged module. Native member
completion/signature help/definition/hover and native alias hover/definition use
the visible module unit for the current document, avoiding duplicate native
cache deserialization while keeping symbol identity local to the file being
edited.
Native class-alias target lookup now builds one reusable native class index per
request path instead of rebuilding it for every native type. This removed the
header-heavy quadratic behavior that made `dudu-webserver` warm hover,
references, completion, and semantic tokens take roughly one second each.
`scripts/probe_lsp_dogfood_latency.py` measures cold document-symbol indexing
and first native-aware hover separately from warm definition, hover, references,
completion, and semantic-token latency for `raymarch-dd` and `dudu-webserver`.
Optional LSP probes cover real `sqlite3` and `raylib` headers through
`pkg-config`, plus `SDL3` and `GLFW` when installed, including diagnostics,
completion, signature help, definition, and hover. The same optional probe now
also checks real C++ standard-library editor intelligence without `pkg-config`
by importing `<vector>` and asserting `std.` completion, `std.vector`
hover/definition, and `std.vector[i32].push_back(...)` signature help against
the system STL headers. They are part of `scripts/test_full.sh` so the broad
developer sweep exercises real-header editor behavior as well as native
compile/run probes.

Status: `scripts/test_lsp_matrix.sh` now builds a deterministic temporary
multi-file workspace and drives JSON-RPC requests for module aliases,
selective imports, direct module imports, transitive import graph loading,
classes, instance methods, fields, constants, enums, sum-type variants,
generic functions/classes/methods, overloaded operators, locals,
module-qualified completion, native headers, native functions, native template
functions, native types, native macros, and missing import diagnostics.
`scripts/test_lsp.sh` runs this matrix after the older smoke suite.

## Non-Goals

- implementing a separate parser in TypeScript
- replacing Clang for C/C++ header understanding
- promising perfect macro expansion navigation for every C/C++ macro shape
- jumping to generated C++ as the primary definition target when a real Dudu or
  native source location exists
