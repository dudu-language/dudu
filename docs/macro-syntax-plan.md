# Dudu Macro System Plan

This document is the canonical design for user-defined Dudu macros. It replaces
the earlier collection of candidate quote syntax, expression-macro sketches,
and unresolved execution models.

Status: specified, not implemented. Compiler-known decorators such as `@test`,
`@operator`, `@constexpr`, and `@extern_c` already exist, but they are language
features rather than user-defined macros.

The target surface is Python-shaped. The expansion model is a typed,
deterministic, additive source generator over Dudu declarations.

Macro performance requirements are specified in
[Macro Performance Plan](macro-performance-plan.md). Serde and third-party
adapter design is discussed in
[Protocols And Serde Design Notes](protocols-serde-design-notes.md).

## Goals

- Remove repetitive declaration boilerplate.
- Support serde-like derives, debug formatting, reflection metadata, command
  parsers, binary schemas, and native binding tables.
- Keep every macro invocation visible in ordinary Dudu syntax.
- Generate normal typed Dudu AST nodes, never source strings.
- Preserve useful diagnostics, navigation, hover, references, and formatting.
- Make unchanged macro expansions effectively free during incremental builds.
- Keep macro packages portable across Dudu compiler internals.

## Non-Goals

- Text substitution like the C preprocessor.
- Custom grammar or local language dialects.
- Lisp-style quote and unquote syntax.
- Rewriting or deleting source declarations.
- Expression and control-flow macros.
- Installing new subtype, overload-resolution, or type-inference rules.
- Using internal compiler AST classes as a public package API.

Ordinary functions, generics, `@constexpr`, and explicit native imports remain
the tools for expression-level abstraction and compile-time computation.

## Design Sources

Dudu should combine the useful parts of established systems without copying
their avoidable complexity.

### Python

Python decorators provide the desired call-site appearance. Python decorators
operate on runtime objects; Dudu decorators execute during compilation and
produce typed declarations instead.

### Rust

Rust establishes useful derive, attribute, and helper-attribute shapes. Its
procedural macros use stable token streams, are unhygienic, require dedicated
proc-macro crates, and execute with build-script-like host access. Dudu adopts
the derive ergonomics but uses hygienic typed AST values and a constrained
worker protocol.

Reference: <https://doc.rust-lang.org/stable/reference/procedural-macros.html>

### C#

Roslyn source generators establish two important rules: generation should be
additive, and incremental generators should model their inputs explicitly.
Dudu follows those rules but returns AST nodes rather than generated source
strings.

References:

- <https://github.com/dotnet/roslyn/blob/main/docs/features/source-generators.cookbook.md>
- <https://learn.microsoft.com/en-us/dotnet/api/microsoft.codeanalysis.iincrementalgenerator>

### Swift

Swift attached macros are additive and their input and output are checked by
the compiler. Swift may reuse an external macro process for multiple
expansions. Dudu uses the same additive and reusable-worker principles without
adding a separate freestanding `#macro` syntax.

References:

- <https://docs.swift.org/swift-book/documentation/the-swift-programming-language/macros/>
- <https://docs.swift.org/swift-book/ReferenceManual/Expressions.html#ID809>

### Nim, Scala, And Elixir

Nim demonstrates the power of typed AST macros. Scala demonstrates that typed
quotes and splices can be principled but create substantial staging machinery.
Elixir demonstrates why quote/unquote is natural when source already has a
compact data representation. Dudu takes typed AST access from this family, but
uses explicit builders because Dudu source is not naturally source-as-data.

References:

- <https://nim-lang.org/docs/macros.html>
- <https://docs.scala-lang.org/scala3/reference/metaprogramming/index.html>
- <https://elixir-lang.org/getting-started/meta/quote-and-unquote.html>

### C And C++

C and C++ preprocessors remain native interop behavior. C++ templates,
`constexpr`, and `consteval` provide compile-time type and value computation,
not a standard procedural declaration-AST macro system. Dudu-defined macros do
not expose preprocessor replacement tokens or duplicate C++ template behavior.

## User Surface

### Derives

`@derive(...)` applies one or more imported derive macros to a declaration.

```python
from json_macros import Json
from debug_macros import Debug

@derive(Debug, Json)
class Player:
    id: u64
    name: str
    hp: i32
```

Each derive receives the original `Player` declaration and its resolved
declaration metadata. It may add members or sibling declarations. It cannot
replace `Player`.

### Helper Attributes

Helper attributes are inert typed metadata consumed by an enclosing macro.

```python
@derive(Json)
class Player:
    id: u64

    @Json(name="displayName")
    name: str

    @Json(skip=True)
    cached_score: i32
```

`@Json(...)` is not a runtime function call. The compiler resolves it to the
helper-attribute schema exported by the imported `Json` macro. Unknown names,
wrong argument types, duplicate attributes, and attributes on invalid targets
are Dudu diagnostics.

The same shape applies to other macro packages:

```python
@derive(Args)
class Options:
    input: str

    @Args(short="o")
    output: str

    @Args(default=False)
    verbose: bool
```

### Attached Declaration Macros

A macro may attach directly when grouping it under `@derive` would be
misleading.

```python
@reflect
class Transform:
    position: Vec3
    rotation: Quat
    scale: Vec3
```

```python
@plugin_exports(prefix="dudu")
class PluginApi:
    def init(app: *App) -> i32:
        return 0

    def tick(app: *App, dt: f32):
        update(app, dt)
```

Attached declaration macros follow the same additive rules as derives.

### Enum Derives

```python
@derive(Debug, StringEnum)
enum Direction:
    North
    South
    East
    West
```

A derive may add methods or sibling functions such as `to_string` and
`from_string`. Payload variants remain structured enum declarations and are
visible through the public macro AST.

## Defining A Macro

Macro definitions are ordinary Dudu functions in macro modules. `@macro`
changes their compilation target and validates their public signature.

```python
import dudu.ast as ast

class JsonOptions:
    name: Option[str] = None
    skip: bool = False
    rename_all: Option[str] = None

@macro(attributes=JsonOptions)
def Json(item: ast.ClassDecl) -> ast.Expansion:
    if not item.is_class():
        ast.error(item.range, "Json can only derive on classes")

    out = ast.expansion(item)
    out.add_method(make_to_json(item))
    out.add_sibling(make_from_json(item))
    return out
```

The first parameter type defines the accepted declaration kind. The return type
must be `ast.Expansion`. `attributes=JsonOptions` exports the named-field schema
used by every `@Json(...)` helper attribute. Option types, defaults, and required
fields come from the ordinary class declaration. The compiler checks argument
names and types before launching the macro.

The macro reads helper metadata through typed AST accessors:

```python
class_options = item.attribute[JsonOptions](Json)

for field in item.fields:
    options = field.attribute[JsonOptions](Json)
    if options.skip:
        continue
```

Placement rules that depend only on node kind are declared with the macro
export. Semantic rules such as "rename is invalid when skip is true" are normal
macro diagnostics. A macro with no helper attributes uses bare `@macro`.

A package exports the macro name, accepted declaration kind, helper-attribute
schema, and implementation entry point from this declaration. Macro imports use
normal Dudu package and module resolution.

Macro modules may import deterministic Dudu helper modules and declared host C
or C++ dependencies. Native dependencies make the macro package host-specific,
are trusted build code, and participate in binary cache keys. `cpp(...)` remains
an explicit native implementation escape hatch; it cannot be used to return
unstructured source as macro output.

## Public Macro AST

Macros compile against a versioned public schema named `dudu.ast`. They do not
link against parser, semantic-analysis, or code-generation internals.

The public read model includes:

- `ClassDecl`, `EnumDecl`, `FunctionDecl`, and `FieldDecl`
- generic type and value parameters
- base classes and method modifiers
- structured `TypeRef` and expression nodes
- decorators and typed helper attributes
- documentation and source ranges
- resolved symbol identities where declaration-level resolution is complete

The public builder model includes:

- declarations, fields, methods, parameters, and generic parameters
- blocks and ordinary statement nodes
- names, literals, calls, members, operators, and index expressions
- type references and generic instantiations
- generated diagnostics and source-origin annotations

Builders must be compact enough that normal derives do not become walls of AST
plumbing. Domain helpers may be ordinary Dudu libraries built on top of the
primitive builders.

Raw tokens and source strings are not accepted macro output. Native spelling is
available only through explicit native-boundary nodes already supported by the
language.

## Additive Expansion Rules

A macro may add:

- methods to the attached class or enum
- sibling classes, enums, functions, constants, and namespaces
- implementations of existing abstract contracts
- metadata declarations
- diagnostics

A macro may not:

- remove, rename, or replace source fields or methods
- rewrite a function body supplied by the programmer
- change the type of a source declaration
- change visibility or layout unless a compiler-known language decorator
  already permits it
- create another macro invocation for recursive expansion
- introduce a new grammar form

Conflicts between generated and source names are errors. Conflicts between two
macro outputs identify both macro invocations and both generated declarations.

## Expansion Order

1. Parse every source module into the normal Dudu AST.
2. Resolve imports, macro identities, and helper-attribute schemas.
3. Resolve declaration-level types and native identities needed by macro input.
4. Validate and load the required macro workers.
5. Give each macro an immutable view of the original source declaration.
6. Collect all additive expansions without exposing one macro's output to
   another macro on the same declaration.
7. Merge generated declarations and diagnose conflicts.
8. Run normal name resolution, semantic analysis, lowering, and codegen over
   source and generated nodes together.

`@derive(A, B)` is deterministic but not a transformation pipeline. Both
macros inspect the same original declaration. A macro that needs shared work
imports an ordinary helper module instead of depending on another expansion.

## Hygiene

Generated symbols carry stable compiler identities. A generated local named
`value` cannot capture a source local named `value`, and a source name cannot
capture a generated helper accidentally.

Public generated names are explicit and checked for collisions. Private helper
names are identity-based and receive stable rendered spellings only during
code generation.

## Execution Architecture

Macro modules compile to host-native workers. They are cached independently of
the target program and reused for all compatible expansions in a compiler or
LSP session.

The compiler and worker communicate through a versioned macro protocol:

- protocol and compiler versions are negotiated before expansion
- input is the public immutable AST schema, never compiler-owned pointers
- output is a public `ast.Expansion`
- diagnostics and source origins travel through the same protocol
- messages have explicit size and recursion limits
- a worker crash, timeout, or malformed response becomes a normal diagnostic

The protocol uses a schema-generated, length-prefixed binary encoding with
stable numeric field tags and unknown-field skipping. The public macro AST
schema is the single source used to generate the compiler encoder/decoder and
the Dudu macro SDK types. Each frame includes a magic value, protocol version,
request ID, message kind, payload length, and bounded payload. JSON and ad hoc
compiler-struct serialization are not protocol implementations.

The worker process is persistent so startup is not paid per declaration. A
macro package binary is rebuilt only when the package, its helper dependencies,
the macro SDK, or its host compilation settings change.

Macro dependencies are trusted build dependencies, but their default language
surface is deterministic. Host-native code does not erase this trust boundary;
package review and lockfile identity remain security requirements.

Capabilities are declared per macro package in `dudu.toml`. The accepted
capability names are `fs.read`, `fs.write`, `env.read`, `process`, `network`,
`clock`, and `random`. Paths, environment names, and executable names must be
narrowed by manifest values where applicable. Undeclared access is denied by
the worker host where the operating system supports enforcement and diagnosed
by the macro SDK on every platform.

`fs.read` inputs are content-hashed. `env.read` values are captured. Writes,
processes, network access, clocks, and randomness make an expansion
non-cacheable unless a package supplies an explicit reproducible input/output
contract supported by the compiler. Non-cacheable macros are permitted only
when named in project configuration and produce a build warning.

## Incremental Caching

Macro binary cache keys include:

- macro package source and helper-module hashes
- Dudu compiler and macro protocol versions
- host toolchain identity and host compilation settings
- declared macro capabilities

Expansion cache keys include:

- macro binary identity and entry point
- canonical input declaration AST hash
- typed helper attributes
- relevant resolved symbol and native metadata hashes
- compile-time configuration values read by the macro
- declared external input content hashes

An unrelated source edit must not invoke an unchanged macro. Changing one
decorated declaration invalidates that declaration's expansions and semantic
dependents, not every use of the macro package.

## Diagnostics And Editor Behavior

Every generated node records its macro invocation, macro definition, and source
declaration origin. Diagnostics show the shortest useful chain:

```text
src/player.dd:4:1: error[dudu/macro]: Json cannot serialize field `socket`
@derive(Json)
^^^^^^^^^^^^^
note: unsupported field declared here
    socket: TcpSocket
    ^^^^^^
note: reported by Json at macros/json.dd:18:9
```

The LSP must provide:

- hover and go-to-definition for macro names and helper attributes
- completion and signature help from helper-attribute schemas
- semantic highlighting for macro invocations
- references and rename by macro symbol identity
- generated-member completion after expansion
- go-to-origin from generated declarations
- diagnostics that remain available after an incomplete source edit

The compiler must provide inspectable expansion commands:

```text
duc expand src/main.dd
duc expand src/main.dd --macro Json
duc expand src/main.dd --show-origins
```

Output is formatted Dudu, with optional source-origin annotations. Generated
C++ remains available through the ordinary emission tools.

## Implementation Sequence

1. Define the versioned public AST read model, builders, protocol, and SDK.
2. Parse and resolve user-defined macro imports, `@macro`, `@derive`, typed
   helper-attribute schema classes, and manifest capabilities.
3. Build the host-native macro worker, lifecycle management, capability checks,
   and binary cache.
4. Implement immutable declaration input, additive expansion collection,
   hygiene, conflict checks, and source-origin tracking.
5. Integrate generated nodes into ordinary name resolution, semantic analysis,
   lowering, module emission, and incremental dependency tracking.
6. Add diagnostics, `duc expand`, formatter support, and complete LSP behavior.
7. Add Debug, Json, StringEnum, reflection, CLI argument, binary schema, and C
   export-table fixtures as external macro packages or test packages.
8. Satisfy every budget in the macro performance plan and add regression
   reporting to `dudu bench compiler`.
9. Audit the implementation for internal AST exposure, source-string output,
   macro-specific type-system rules, hidden nondeterminism, and stale syntax.

## Acceptance

- The user examples in this document parse and resolve through ordinary imports.
- Macro authors use only the public typed AST API.
- Expansions are additive, hygienic, deterministic, and source-mapped.
- Generated declarations pass through the same semantic pipeline as source.
- Helper attributes are typed and editor-visible.
- Unchanged builds invoke no macro implementations.
- Macro workers are persistent, bounded, crash-isolated, and inspectable.
- `duc expand` displays formatted generated Dudu with origins.
- Native C/C++ macros remain a separate interop mechanism.
- Quote/splice syntax, source-string generation, and custom grammar are absent.
- The performance benchmark suite meets its release budgets.
