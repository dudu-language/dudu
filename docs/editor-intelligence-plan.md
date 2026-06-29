# Dudu Editor Intelligence Plan

Dudu should feel like a serious compiled language in VS Code and other LSP
editors. Syntax coloring is not enough. The target experience is closer to
Rust Analyzer or clangd: the editor should know what symbols mean, where they
come from, and how to navigate through Dudu and imported C/C++ code.

This plan is part of the ProjectIndex/LSP work. It is not cosmetic theme work.
The language server must expose real compiler facts, and the editor extension
must wire those facts through standard LSP features.

## Current Gap

The current `dudu-lsp` server advertises semantic tokens and already
implements many hover, definition, references, completion, and diagnostic
requests. The local VS Code extension now uses VS Code's standard
`vscode-languageclient` package instead of the old broad hand-written JSON-RPC
client, so normal LSP capability registration is available to the editor.
Remaining gaps are server/index coverage, richer symbol facts, and making every
position-based request fast and identity-aware.

Some intelligence also remains incomplete server-side:

- go-to-definition should work for functions, constants, fields, methods, enum
  values, sum-type variants, module aliases, selective imports, and imported
  Dudu library symbols
- go-to-definition should jump through imported C/C++ headers when scanned
  source locations are available
- hover should show full Dudu type/signature information, not just a symbol name
- hover should attach Dudu `#` declaration comments and Python-style docstrings
- hover should show native C/C++ signatures and documentation when available
- find-references should use symbol identity, not only spelling
- semantic highlighting should distinguish declarations, locals, parameters,
  fields, methods, types, functions, constants, macros, native symbols, and
  unresolved symbols

Recent status: go-to-definition for parameters, inferred assignment locals,
loop bindings, and destructured bindings is now covered in direct LSP tests and
JSON-RPC smoke fixtures. Dogfood latency probing exists in
`scripts/probe_lsp_dogfood_latency.py`; after sharing the native class-alias
index across hover/symbol/semantic-token paths, warm editor requests in
`raymarch-dd` and `dudu-webserver` are in the single-digit/tens-of-ms range on
the current machine, while cold indexing remains the larger cost. The dogfood
probe also covers same-file local references, and hover can now infer locals
assigned from native C/C++ function calls through the native-enriched module
view.
Find-references for local variables now scopes same-named bindings to the
selected function/method body in direct LSP tests, so unrelated locals with the
same spelling are not reported together.
Find-references for Dudu member declarations now uses a class-qualified
`Class.member` query internally and filters member expressions by receiver
type, so same-named fields or methods on unrelated classes are not reported
together. Member use sites such as `player.hp` and `player.move(...)` now
resolve through the receiver's structured type to the same qualified member
identity, including when the class was imported from another Dudu module.
Enum value declaration references use the same qualified identity shape, so
`Mode.Play` does not collide with another enum's `Play` variant.
Dudu semantic tokens now use a source-symbol index for Dudu classes, enums,
enum members, implicit local bindings, and member calls instead of relying only
on syntax shape or native metadata. A decoded semantic-token fixture asserts
token text, kind, and modifiers against the original source for the core Dudu
token classes, including native type/function/value/macro tokens with the
native modifier. The LSP server now builds semantic tokens with ProjectIndex
import context, so imported Dudu module aliases, selective imported classes,
imported functions, and imported constants receive namespace/class/function/
readonly-variable token identities instead of syntax-only fallback coloring.
The JSON-RPC LSP matrix decodes those token streams and asserts the imported
symbol kinds.
Unmatched expression names, callees, and member fallbacks now carry the
`unresolved` semantic-token modifier while known locals stay unmarked, giving
themes and diagnostics a stable server-side distinction for unknown symbols.
Leading `#` declaration comments and first-body triple-single-quoted
docstrings for modules, functions, methods, classes, and enums now attach to
parsed Dudu declarations and flow through hover for same-file and imported Dudu
symbols.
Completion items for Dudu symbols also carry those AST docs through
current-document, imported, module, and member completion paths. Hover no longer
recovers declaration comments by scanning source lines at request time, and
completion resolve preserves documentation attached to the item instead of
fabricating docs from display text. Signature help also surfaces those docs for
Dudu functions from the visible symbol index. Document symbols now use LSP
`DocumentSymbol` shape and include a short AST doc summary in their detail text
when available. The LSP matrix fixture now exercises doc propagation for
classes, fields, field member hover, methods, imported completions, signature
help, and document symbols through actual JSON-RPC requests, including
docstring-backed module, class, enum, enum-value, method, and function symbols.
Hover for selective imports now preserves suffix identity, so `Mode.Play`
resolves to the imported enum variant docs instead of falling back to spelling
or only handling the imported `Mode` name.
Native hover now surfaces the scanner's canonical native identity key when the
symbol has one, so hover shows both the Dudu-shaped signature/type and the
identity used by native references. Native function hover/detail also includes
a compact scanner-derived native signature suffix when concrete
return/parameter spelling is available. The native scanner now runs Clang with
parsed comments and attaches scanned header comments to native declarations, so
hover can show real C/C++ declaration docs when the header exposes them.
Native find-references now consults `ProjectIndex`'s per-module native identity
table instead of rebuilding LSP symbol lists for every candidate document. Open
single-file editor overlays are indexed with their entry path, so unsaved or
not-yet-written files can still take part in native identity filtering.

## Target Behavior

### Dudu Source

For source in the current project or a Dudu library:

- clicking a local variable jumps to the binding that introduced it
- clicking a parameter jumps to the parameter declaration
- clicking a function call jumps to the function definition
- clicking a class/type name jumps to the class/type declaration
- clicking a field access jumps to the field declaration when the receiver type
  is known
- clicking a method call jumps to the method definition when the receiver type is
  known
- clicking an enum value or sum-type variant jumps to the variant declaration
- clicking `import module` or `from module import name` jumps to the module file
  or imported declaration
- hover shows kind, type/signature, module path, visibility, docs, and relevant
  native identity if the symbol comes from native code

### Native C/C++ Interop

For imported native code:

- `import c "SDL3/SDL.h"` and `import cpp <vector>` should produce navigable
  native symbols when the scanner has source locations
- clicking a C function, macro, enum constant, struct type, typedef, C++ class,
  namespace, method, field, constructor, or overload should jump to the header
  declaration when the declaration is real and not only synthesized
- hover should show the scanned C/C++ spelling plus the Dudu-shaped type when
  useful
- native docs should be shown when Clang/header metadata can recover useful
  comments; otherwise hover should honestly show signature-only information
- native source locations should point at headers, not generated C++
- synthesized symbols and macro-generated declarations should be labeled as such

### Standard Library And Installed Sources

Dudu should eventually be able to jump into installed Dudu source the way Rust
can jump into installed Rust library source:

- built-in library modules should live as real `.dd` source where possible
- installed source paths should be known to the project index
- hover and definition should work on standard library symbols exactly like
  project symbols
- generated/compiler-only built-ins should clearly say they are built in and
  should not pretend to have a source file

## VS Code Client Work

The serious extension path is to use VS Code's standard language-client package
instead of maintaining a broad hand-written LSP client.

Required work:

- keep TextMate highlighting as a baseline for startup and non-LSP editors
- use `vscode-languageclient` for `dudu-lsp`
- let the standard client wire diagnostics, hover, definition, references,
  rename, completion, signature help, formatting, code actions, document symbols,
  workspace symbols, and semantic tokens
- register semantic-token support so VS Code requests
  `textDocument/semanticTokens/full`
- keep command palette actions for `dudu build`, `dudu run`, `dudu test`,
  `dudu fmt`, and LSP restart
- preserve the status item showing compiler path, target, and native-header
  state

The extension must not duplicate compiler logic in JavaScript. If a feature
needs type or symbol knowledge, that knowledge belongs in `dudu-lsp`.

## Server Work

The server should provide the editor behavior from shared compiler state:

- ProjectIndex owns parsed modules, exported declarations, import graph,
  reverse references, native metadata, source overlays, and dirty state
- cursor selection returns a structured query containing symbol name, dotted
  path, expression path, and source range
- definition, hover, references, rename, completion, signature help, and semantic
  tokens consume that shared query and shared indexed module view
- native metadata keeps canonical identity keys plus source file/range when
  Clang provides them
- doc comments/docstrings attach to AST declarations rather than being recovered
  by line scanning in hover
- warm editor requests reuse cached ProjectIndex views instead of reparsing and
  remerging native headers
- unresolved symbols return useful diagnostics or empty results quickly; they
  must not leave the editor spinning

## Semantic Highlighting

Semantic tokens should be type-aware and stable enough that a normal theme can
color Dudu like a real language.

Minimum token classes:

- namespace/module
- type/class/enum/sum type
- function/method
- local variable
- parameter
- field/property
- constant/static field
- enum member/variant
- macro
- keyword/operator/string/number where semantic tokens add value

Minimum modifiers:

- declaration/definition
- readonly
- static
- native
- unresolved

Semantic token tests should assert decoded tokens by name, kind, and modifier in
small fixtures rather than only checking that some token data exists.

Status: direct frontend coverage now decodes semantic-token deltas back to
source text and asserts the actual token name, kind, and modifiers for module
constants, classes, fields, static fields, methods, parameters, implicit locals,
functions, enums, enum members, unresolved names/callees/member fallbacks,
numbers, and strings. The JSON-RPC LSP matrix also decodes server semantic-token
responses and asserts imported symbol identities plus unresolved-token
modifiers through `dudu-lsp`.

## Validation

Validation must include deterministic LSP JSON-RPC tests and dogfood repos.

Required fixture families:

- same-file locals and parameters
- imported Dudu functions/classes/constants
- module aliases and selective imports
- fields and methods on Dudu classes
- generic classes/functions
- enum members and sum-type variants
- overloaded operators
- native C functions/types/macros
- native C++ classes/methods/templates/namespaces
- missing/unresolved symbol behavior
- doc comments and docstrings
- semantic-token classifications

Dogfood checks:

- `/home/vega/Coding/Graphics/raymarch-dd`
- `/home/vega/Coding/Web/dudu-webserver`

Measure:

- cold workspace index latency
- warm hover latency
- warm go-to-definition latency
- warm find-references latency
- warm completion latency
- warm semantic-token latency

## Priority

This is on-topic for the active ProjectIndex goal. It should come after the
current module/incremental backend is stable enough that editor requests can rely
on the same indexed project model. It should not wait until all syntax features
are complete, because poor editor intelligence makes dogfooding painful and hides
compiler-model bugs.
