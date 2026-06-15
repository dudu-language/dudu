# AST Plan

Dudu needs a real statement and expression AST. The current compiler has a
real lexer, parser, module AST, class/function/import declarations, and nested
raw statement blocks. That was enough to prove the language shape, C/C++
interop, and generated C++ output. It is not enough for the next level of
errors, generics, macros, semantic highlighting, and reliable refactors.

The goal is not to make the compiler academic. The goal is to stop treating
function bodies as strings.

## Current Shape

Already structured:

- tokens and indentation
- imports
- classes
- fields
- methods
- functions
- parameters
- enums
- constants
- decorators
- native header metadata
- direct parser construction of statement nodes for function and method bodies
- parsed `Stmt` bodies stored on functions and methods
- initial expression shape capture for common names, literals, calls,
  template calls, member/index/slice access, unary/binary operators,
  conditionals, and collection literals
- initial type shape capture for names, qualified names, templates, pointers,
  references, wrappers, fixed arrays, and function-like type signatures
- function pointer/callback signature parsing can consume parsed `TypeRef`
  nodes directly, including omitted-return `fn(...)` as `void` and wrapper
  templates such as `std.function[fn(...)]`
- local scopes preserve parsed `TypeRef` nodes for declared parameters,
  constants, locals, catch bindings, and typed loop bindings, so function
  pointer calls can check signatures without reparsing declared type text
- type aliases preserve parsed `TypeRef` nodes in the symbol table, allowing
  local callback aliases such as `type Visit = fn(...)` to resolve through the
  structured type path
- callback alias lookup follows parsed `TypeRef` alias chains with a cycle
  guard before falling back to resolved type text
- type-shaped builtins such as `new[T]`, `malloc[T]`, `sizeof[T]`,
  `alignof[T]`, and `offsetof[T]` validate parsed `TypeRef` arguments
  recursively, so nested unknown types get precise diagnostics
- the legacy raw-callee allocation fallback reparses bracketed allocation types
  into `TypeRef` before validation, keeping nested diagnostics consistent while
  callers migrate to parsed template calls
- source range fields on statement, expression, and type nodes
- semantic diagnostics for return values, local initializer values, local type
  names, conditions, and assignment targets use expression/type node locations
  where available
- initial `TypeRef` C++ lowering is implemented and used for type aliases
- declaration validation walks parsed `TypeRef` nodes recursively for aliases,
  enum underlying types, fields, parameters, returns, constants, and static
  fields, so nested unknown types in containers/callbacks are diagnosed at the
  nested type source location
- declaration type-shape validation, including Dudu tuple arity limits, also
  walks `TypeRef` nodes instead of re-splitting type strings
- function-body validation uses the same recursive `TypeRef` checks for local
  variable annotations, catch bindings, and typed `for` loop bindings
- template calls keep template arguments separate from runtime call arguments,
  and C++ emission lowers them from the parsed expression node
- empty parsed template calls such as `identity[]()` are rejected during
  semantic checking instead of falling through raw expression inference and
  emitting invalid C++
- unresolved parsed dotted template calls such as `missing.namespace[i32]()`
  are rejected after native/header lookup misses, while declared native import
  prefixes still return `auto` explicitly
- unsupported parsed template callees such as `(1)[i32]()` are rejected instead
  of being emitted as raw C++
- template calls also keep parsed `TypeRef` nodes for bracketed arguments, so
  type-shaped builtins such as `new[T]`, `malloc[T]`, `sizeof[T]`,
  `alignof[T]`, `offsetof[T]`, and empty `list[T]`/`dict[T]`/`set[T]`
  construction can check and emit type arguments without reparsing them as
  expression text
- `TypeRef` supports compile-time value template arguments such as the `3` in
  `std.array[i32, 3]`, so native C++ templates with integer non-type
  parameters do not masquerade as missing Dudu types
- ordinary calls and template calls keep a parsed callee expression alongside
  the compatibility `name` field, so sema/emission/LSP can migrate away from
  raw callee strings incrementally
- ordinary call C++ emission lowers parsed callee expressions instead of
  lowering the callee name string directly
- template-call fallback emission and pointer-cast emission use parsed callee
  helper paths for non-keyword callees
- templated pointer casts such as `*const[i32](ptr)` validate parsed
  `TypeRef` arguments recursively and lower through the AST template-call path
  instead of falling into generic template-call emission
- `offsetof[T](field)` validates the field designator through the parsed field
  expression and lowers bare, dotted, and string field names structurally
  instead of emitting raw child expression text
- ordinary call semantic inference uses a callee lookup string reconstructed
  from the parsed callee expression where possible
- parsed method-call inference handles typed expression receivers such as
  `make_counter().get()`, including Dudu-source diagnostics for unknown
  methods, instead of falling back to raw expression inference
- unresolved parsed dotted calls such as `missing.namespace()` are rejected
  during semantic checking after native/header lookup misses instead of being
  emitted as raw C++
- unrecognized parsed call and template-call expressions now produce Dudu
  diagnostics after structured lookup misses instead of falling through to raw
  expression inference; `cpp(...)` remains the explicit native escape hatch
- imported native namespace calls without scanner metadata, and method calls on
  local `auto` receivers from native APIs, stay on parsed call nodes and return
  `auto` explicitly instead of using raw expression fallback
- final local-method lookup recognizes string-like native C++ types such as
  `std.string_view`/`std::basic_string_view` for common `size`, `length`, and
  `empty` calls instead of relying on raw fallback
- direct parsed lambda calls such as `(lambda value: value + 1)(41)` stay on
  parsed call nodes, while unsupported non-callable callees such as `(1)(2)`
  are rejected instead of emitted through raw expression fallback
- template-call semantic inference and template method lookup use the parsed
  callee expression when reconstructing lookup names
- generated local type inference for call expressions derives callee lookup
  names from parsed callee nodes
- generated local type inference no longer has a raw-string helper for
  `Unknown` expression nodes; unsupported expression shapes are rejected before
  emission instead of guessed from text
- parsed member/callee path reconstruction lives in a shared AST expression
  helper instead of duplicated sema/emission utilities
- AST type compatibility checks use parsed call callees for explicit casts and
  `Ok(...)`/`Err(...)` result construction
- semantic highlighting uses parsed call callees so method calls can color the
  receiver and called member separately instead of treating dotted callees as
  one raw span
- array/list/dict index type inference uses parsed index expressions where
  available, so tuple-shaped multi-index expressions no longer depend on raw
  comma splitting
- native C/C++ overload checks for ordinary and explicit-template parsed calls
  consume expression children directly instead of flattening arguments back to
  strings
- constructor and native overload validation expose only AST argument APIs; the
  old string-vector overloads and string matching helpers have been removed
- legacy string expression inference parses call, method, operator, and index
  arguments into `Expr` nodes before using shared semantic checks; the old
  string call-argument checker has been removed
- parsed `new[T]` and `malloc[T]` allocation calls validate argument counts
  from expression children instead of stringified call arguments
- AST-backed statement checks route assignment compatibility through expression
  nodes instead of using raw statement value strings
- compound-assignment target checks use the parsed target expression path
  instead of feeding the target text back into legacy target analysis
- type compatibility has AST overloads for simple literals and list/set/dict
  literals, including expected-type disambiguation for empty `{}` dict
  initializers, reducing reliance on string parsing for assignment checks
- AST literal classification now falls back to raw-text literal parsing only for
  `Unknown` expression nodes; parsed non-literal nodes are not reclassified by
  string heuristics
- C++ assignment emission detects `Option` reset from `NoneLiteral` expression
  nodes instead of raw value text
- generated local C++ type inference uses parsed expression nodes and no longer
  has a raw-string helper for `Unknown` expression shapes
- generated local C++ type inference also follows parsed index expressions for
  local `list`, `dict`, `set`, `span`, and shaped `array` receivers, so common
  `value = items[i]` assignments do not fall back to raw expression text
- generated local C++ type inference follows parsed index expression receivers,
  so `value = make_values()[0]` and chained fixed-array row indexes can infer
  types without raw expression parsing
- generated local C++ type inference handles parsed literals, unary
  address/deref/not/minus expressions, binary expressions, conditionals, and
  parsed template callees conservatively, so common `value = count + 1` style
  locals do not need raw expression inference
- C++ `if constexpr` detection for build-only conditions uses parsed condition
  expressions, with raw text scanning only as an unknown-expression fallback
- C++ expression emission writes string, integer, and float literal expression
  nodes directly, and lowers `str(value)` calls through an explicit AST path
  instead of sending them through raw expression lowering
- C++ expression emission lowers parsed plain-name nodes directly through
  namespace alias qualification instead of sending them through full raw
  expression rewriting
- nested fixed-array literal scalar emission uses parsed expression nodes, so
  Dudu numeric literal normalization and other scalar expression lowering stays
  consistent inside array initializers
- semantic checks for assignment through `*ptr` use parsed unary target
  expressions before falling back to raw target text
- semantic checks for plain-name and member assignment targets use parsed
  target expressions before falling back to raw target text for complex shapes
- member-path type checks reconstruct paths from parsed name/member/index
  expression nodes instead of directly trusting the original expression text
- parsed member expression inference handles typed expression receivers such as
  `make_point().x`, including Dudu-source diagnostics for unknown fields,
  instead of falling back to raw expression inference
- semantic checks and C++ statement emission use expression-node presence for
  optional statement values/messages instead of raw statement value strings
- semantic compatibility checks for parsed binary and comparison operators use
  the right-hand expression node instead of stringifying it for assignment rules
- native overload matching and constructor semantic checks use parsed argument
  expression nodes for assignment compatibility instead of passing argument text
  back through string-based compatibility callbacks
- assignment compatibility for parsed explicit casts, value-wrapper
  assignments, and `Ok(...)`/`Err(...)` result construction inspects call
  expression nodes instead of rediscovering those forms from raw text
- parsed lambda expressions keep parameter expression nodes and body expression
  nodes; C++ emission lowers lambda parameters and bodies structurally while
  full lambda typing remains future work
- named-argument constructor emission relies on parsed `NamedArg` nodes only;
  the old raw `field=value` child-text fallback has been removed
- parsed call emission lowers Dudu `list.append(...)` calls to C++
  `push_back(...)` at the callee node instead of relying on raw expression
  replacement
- C++ member expression emission lowers proven pointer receivers from parsed
  receiver nodes (`item->field`, `items[i]->field`) before falling back to raw
  member rewriting for unresolved cases
- C++ member expression emission lowers normal value receivers from parsed
  receiver nodes and applies namespace alias qualification directly, avoiding
  the full raw expression rewrite path for `player.field` and `std.sin` shapes
- integer and float literal parsing recognizes underscore separators, and
  integer literals recognize `0x`, `0b`, and `0o` prefixes so systems-style
  constants stay on the literal AST path
- LSP local type inference recognizes the same systems numeric literal shapes,
  keeping hover behavior aligned with compiler literal parsing
- default `assert` message emission uses the parsed condition expression text
  instead of the raw statement condition field
- frontend AST shape tests cover systems integer literals and `Slice`
  expression nodes directly
- nested expression ranges advance through whitespace and delimiters for call,
  template-call, list, dict, set, tuple, index, and slice shapes instead of
  blindly inheriting parent expression starts
- unary, binary, and conditional expression children keep source locations on
  the child expression text rather than on the outer expression
- local address escape checks walk parsed return/call/unary expression nodes
  for `return &local` and container `.append(&local)` / `.push_back(&local)`
  cases instead of scanning raw statement text
- parsed index expression type inference uses the AST receiver and index nodes
  regardless of whether the caller supplied an explicit diagnostic location
- parsed index expression type inference handles expression receivers such as
  `make_values()[0]` and chained array rows without falling back to raw
  expression inference
- malformed parsed index expressions such as `values[]` are rejected during
  semantic checking instead of being emitted through raw expression fallback
- build flag validation walks parsed expression nodes for constants,
  `static_assert`, and normal statements; raw text scanning remains only for
  unknown statements and explicit C++ escape statements
- unsupported Python call checks for `eval`, `exec`, `getattr`, and `setattr`
  walk parsed call expressions, with raw text scanning kept for unsupported or
  unknown expression shapes
- unsupported `await` and expression-level `yield` forms parse into AST nodes
  and are rejected by the unsupported-feature pass without relying on raw text
  scanning
- malformed lambda expressions are rejected during semantic checking instead of
  being emitted through the raw expression fallback and left to the C++ compiler
- standalone slice expressions are rejected during semantic checking, and parsed
  expression nodes that fail structural C++ lowering no longer fall back to raw
  expression rewriting
- malformed unary, binary, and conditional expression nodes are rejected during
  semantic checking with Dudu diagnostics instead of falling through legacy
  expression inference

Still too string-based:

- some legacy string-based sema helper overloads remain for older constructor,
  native-call, and explicit `cpp(...)` paths while their callers migrate to AST
  callbacks
- C++ escape hatches and raw macro shapes
- user-facing macro/decorator forms still need deeper AST nodes
- lambda parameter declarations and target-type-aware lambda checking remain
  shallow
- some type compatibility and native-header checks still route through type
  strings after `TypeRef` parsing
- exact original-token ranges inside function bodies; current body-node ranges
  are derived from normalized joined statement text

## Target Architecture

Keep the outer `ModuleAst`, and make function bodies real statements and
expressions all the way through checking and lowering.

Recommended high-level compiler pipeline:

```text
source
  -> tokens
  -> parse tree / AST
  -> name resolution
  -> type checking
  -> lowering checks
  -> C++ AST-ish emit model
  -> generated C++
```

The parser should produce syntax that preserves source locations for every
meaningful node. The type checker should attach resolved types and symbol
links. The emitter should consume typed nodes, not source text.

## Statement AST

Required statement nodes:

- `BlockStmt`
- `ExprStmt`
- `VarDeclStmt`
- `AssignStmt`
- `CompoundAssignStmt`
- `ReturnStmt`
- `IfStmt`
- `MatchStmt`
- `CaseStmt`
- `WhileStmt`
- `ForStmt`
- `BreakStmt`
- `ContinueStmt`
- `TryStmt`
- `ExceptClause`
- `RaiseStmt`
- `AssertStmt`
- `DebugAssertStmt`
- `StaticAssertDecl`
- `CppEscapeStmt`
- `MacroCallStmt`
- `TupleDestructureStmt`

Statements should keep exact source ranges:

- full statement range
- keyword range
- declared name range
- type annotation range
- value expression range

This enables error squiggles on the right token instead of whole-line errors.

## Expression AST

Required expression nodes:

- `NameExpr`
- `LiteralExpr`
- `StringExpr`
- `UnaryExpr`
- `BinaryExpr`
- `CallExpr`
- `TemplateCallExpr`
- `MemberExpr`
- `IndexExpr`
- `SliceExpr`
- `ConstructorExpr`
- `ListLiteralExpr`
- `DictLiteralExpr`
- `SetLiteralExpr`
- `TupleLiteralExpr`
- `LambdaExpr`
- `ConditionalExpr`
- `CastExpr`
- `SizeofExpr`
- `AlignofExpr`
- `OffsetofExpr`
- `NewExpr`
- `MallocExpr`
- `CppEscapeExpr`
- `MacroCallExpr`

Expressions should carry:

- source range
- resolved type after checking
- constant-value info when known
- resolved symbol when applicable
- value category where it matters for C++ interop

## Type AST

Type strings should also become structured.

Required type nodes:

- `NamedType`
- `QualifiedType`
- `TemplateType`
- `PointerType`
- `ReferenceType`
- `ConstType`
- `VolatileType`
- `AtomicType`
- `DeviceType`
- `StorageType`
- `SharedType`
- `FixedArrayType`
- `FunctionType`
- `TupleType`

This matters for generics, native header interop, semantic highlighting, and
diagnostics. A type like `std.unordered_map[str, list[Player]]` should not be
split with ad hoc string code.

## Diagnostics

AST-backed diagnostics should include:

- source span
- primary message
- optional note spans
- optional fix-it edits
- error code
- diagnostic source such as `dudu/parser`, `dudu/sema`, or `dudu/native`

The target quality is Rust/Java-style practical help:

```text
src/main.dd:12:18: error[dudu/type-mismatch]: expected i32, got str
    total: i32 = name
                 ^^^^
note: `name` was declared here as str
    name: str = "bob"
    ^^^^
help: convert explicitly if this is intentional
    total: i32 = i32(name)
```

Recommended fix-it categories:

- add missing import
- qualify ambiguous name
- insert explicit cast
- replace `.` with pointer member access when the type is a raw pointer, if Dudu
  keeps exposing that distinction
- add missing template argument
- add missing return value
- remove unreachable statement
- correct obvious typo from nearby symbol names
- add missing `&` or `*` in type positions when native call signatures require it

## Semantic Highlighting

TextMate highlighting is not enough. The AST and symbol table should feed LSP
semantic tokens.

Token classes:

- namespace/module
- class/type
- type parameter
- function
- method
- constructor
- parameter
- local variable
- field/member
- static field
- constant
- enum
- enum member
- macro
- decorator
- keyword
- operator
- builtin type
- imported native symbol

Useful modifiers:

- declaration
- definition
- readonly
- static
- deprecated
- unsafe/native
- unresolved
- template

This is how Dudu gets real coloring for args, variables, members, classes, and
native symbols.

Status: initial full-document LSP semantic tokens are implemented from the
parsed AST for Dudu declarations, parameters, fields, locals, types, literals,
calls, and member expressions. Native-symbol semantic coloring still needs to
come from the resolved native header metadata layer.

## LSP Implications

The AST should power:

- diagnostics while editing
- hover with resolved types
- go to definition for Dudu and native symbols
- find references
- rename
- completion
- signature help
- semantic tokens
- document symbols
- workspace symbols
- code actions
- format-aware range edits

The LSP should not reparse everything with shell commands. It should call the
same parser, resolver, and checker APIs used by `duc check`.

## Generics Implications

Native Dudu generics need a typed AST.

The AST must represent:

- type parameters on classes and functions
- template argument lists
- uses of type parameters in fields, params, locals, and return types
- generic method calls
- instantiated function/class signatures
- errors pointing at the failing generic body and the call site that caused the
  instantiation

Without this, generic diagnostics become generated-C++ noise.

## Macro Implications

Macros should operate on syntax nodes or checked declaration metadata, not loose
strings.

The AST should let macros inspect or generate:

- class declarations
- field lists
- function declarations
- enum declarations
- attributes/decorators
- expression nodes for simple expression macros

Raw token macros can remain a C/C++ interop escape hatch. Dudu-native macros
should prefer structured input and structured output.

## Migration Plan

1. Add AST node types alongside the old raw statement bridge: done.
2. Parse simple statements into AST while preserving old lowering for unsupported
   forms.
3. Parse expressions into AST with source ranges.
4. Lower typed AST statements to C++.
5. Move semantic checks from string helpers into AST visitors.
6. Convert formatter and LSP to use AST nodes.
7. Remove raw statement lowering for normal Dudu syntax.
8. Keep `cpp(...)` as the explicit raw C++ escape hatch.

Status: functions and methods now store parsed `Stmt` bodies directly. The old
duplicate `FunctionDecl::body` raw statement storage and raw-body C++/control
flow helper overloads have been removed. The local escape checker also consumes
parsed `Stmt` nodes directly. The parser now constructs `Stmt` nodes directly
instead of staging through a raw statement tree.

Statement semantic checks now enter expression inference through parsed `Expr`
nodes for returns, assignments, local initializer inference, conditions, array
literal element checks, and expression statements. Core AST expression kinds
such as literals, names, unary/binary expressions, tuples, conditionals,
member/index access, and collection literals are handled structurally. Complex
calls still fall back to the old string inference path until the AST grows those
nodes. Binary expression parsing covers logical, comparison, bitwise, shift,
arithmetic, and modulo operators.

Comma-separated expression children now keep their own source columns for call
arguments, list literals, tuple literals, set/dict entries, and template-call
arguments parsed through expression lists.

Dudu-defined function calls, function-pointer calls, static class functions,
Dudu-visible method calls, native calls, constructor calls, and template calls
now type-check parsed argument `Expr` nodes directly.

C++ statement emission now lowers return values, assert/debug_assert
conditions, raise values, if/elif/while conditions, for iterables, and bare
expression statements from parsed `Expr` nodes where those nodes are
structurally reliable.

Assert and debug_assert statements now store separate condition and message
expression nodes. Semantic checks and C++ emission no longer split their
condition/message pieces from raw statement strings.

Except clauses now store structured catch binding and catch type fields.
Semantic checks and C++ emission no longer parse catch headers from raw
statement text.

Simple assignment handling now recognizes fresh local bindings and tuple
destructuring from parsed target expressions. C++ emission uses parsed
assignment value expressions for those paths instead of lowering raw right-hand
side text.

Local variable C++ emission now lowers explicit declaration types through the
parsed `TypeRef`. It still uses computed canonical type text when array literal
shape inference changes the declared type.

Top-level function signatures, generated C headers, class method signatures,
class constants, and static fields now lower declared C++ types through parsed
`TypeRef` nodes.

Condition semantic checks now consume parsed statement condition expressions
directly instead of accepting raw condition text and reparsing on mismatch.

`delete` is now a structured statement node with a parsed operand expression.
Semantic checks, C++ emission, and semantic token collection no longer detect
it by scanning raw statement text.

Compound assignment C++ emission now uses parsed target and value expressions
plus the parsed compound operator instead of normalizing raw statement text.

The main statement semantic dispatcher no longer depends on raw statement text
for break/continue diagnostics.

Structured control-flow C++ emission no longer creates a raw statement text
copy before dispatching.

Return statements and common local list, set, and tuple initializers now use
parsed value expression kinds and children for C++ emission instead of raw
comma splitting and substring extraction.

Typed `for` loop binding C++ emission now lowers the binding type through the
parsed `TypeRef`.

Dict literals now parse key/value pairs as `DictEntry` expression nodes.

Template calls now preserve template arguments separately from runtime call
arguments in `Expr::template_args`. C++ expression emission handles ordinary C++
template calls and built-in template-shaped forms such as `new[T](...)`,
`malloc[T](...)`, `sizeof[T]()`, `alignof[T]()`, `offsetof[T](field)`, and empty
`list[T]()`/`dict[K, V]()`/`set[T]()` value construction without falling back to
the raw expression text.

Assignment C++ emission now handles parsed non-binding targets such as member
paths and index expressions directly from `target_expr` and `value_expr`. Bare
name assignment still owns the local-binding inference path.

Local declaration initializer fallback now lowers from the parsed
`value_expr`. Slice index expressions such as `values[1:4]` parse the `1:4`
range as a `Slice` expression node and emit from the parsed index node,
preserving `span[T]` view lowering without requiring a raw whole-expression
rewrite.

`break` and `continue` statements now emit directly from their `StmtKind`
instead of falling through the unknown-statement raw text path.

Nonempty `Unknown` expressions are rejected during semantic checking instead of
calling the old raw expression inference path. Unknown expressions also no
longer lower as raw C++ text during C++ emission. Use `cpp(...)` for explicit
native escape hatches.

The old raw-text compound-assignment normalization fallback has been removed.
Compound assignment is emitted only through the parsed `CompoundAssign` node;
the statement catch-all no longer emits raw statement text for `Unknown`
statements.

Template-call semantic inference now has an AST path that uses parsed template
arguments and parsed runtime arguments. Native/header interop overload checks
also consume parsed runtime argument nodes for ordinary and explicit-template
calls. Unary address-of and dereference expressions are parsed as `Unary` nodes,
which keeps pointer assignment targets such as `*ptr = value` on the AST-backed
assignment path.

Ordinary constructor and explicit-cast call semantic inference now uses parsed
runtime call arguments. Dotted constructor calls are only treated as constructors
when the callee is a known scanned/imported type, so imported C functions such
as `sdl.SDL_PollEvent(...)` do not get misclassified as type construction just
because the name is dotted.

Named-argument call C++ emission now emits from parsed call children. Named call
arguments such as `Point(x=1)` parse as `NamedArg` expression nodes, and
constructor semantic checks consume those nodes directly instead of
rediscovering named fields from raw argument strings. The old raw named-call
rewrite has been removed for parsed calls.
Common dict initializer C++ emission lowers those parsed entries instead of
splitting literal text again.

Built-in calls such as `len(...)`, `range(...)`, `min(...)`, `max(...)`,
`align_up(...)`, `print(...)`, `delete(...)`, and `free(...)` now have an AST
semantic inference path, including arity/type diagnostics for the checked
built-ins.

Ordinary C++ call emission now lowers parsed callee and argument nodes directly
instead of rebuilding a raw call string. Built-in primitive casts such as
`i32(value)`, `len(value)`, and pointer cast calls such as
`*struct Native(user_data)` have explicit AST emission paths.
Pointer cast calls also have an AST semantic inference path, so native callback
patterns such as `*struct State(user_data)` no longer depend on the raw
expression inference fallback.

Class instance field initializer emission now uses parsed field `value_expr`
nodes instead of raw initializer strings.

Module constants, class constants, static fields, and `static_assert`
declarations now store parsed expression nodes. C++ emission and semantic-token
collection use those declaration expression nodes instead of raw strings.
Enum value initializers now store parsed expression nodes as well; enum C++
emission and semantic-token collection use those nodes.
Simple integer constant evaluation for `static_assert` failure diagnostics uses
parsed expression nodes for names, integer literals, unary minus, and binary
arithmetic/comparisons instead of reparsing expression strings.
Compile-time expression validation for calls to non-`@constexpr` Dudu functions
walks parsed call/template-call nodes in constants and `static_assert`
declarations instead of scanning raw expression strings.

Dudu-native `@operator("[]")` semantic checks now consume parsed index
argument nodes, including tuple-shaped multi-index expressions, instead of
splitting raw index text.

`range(...)` for-loop C++ emission now reads parsed call arguments from the
iterable expression instead of parsing them back out of lowered C++ text.

Simple indexed assignment semantic checks now use parsed `Index` target
expressions when the indexed base is a local name.

Index expression C++ emission now lowers parsed base and index child
expressions directly.

List, set, and tuple literal C++ expression emission now uses parsed literal
children instead of raw literal text.

Bare comma expressions now parse as tuple literals, including Python-style
multi-value returns. The shared AST comma splitter is quote-aware so commas
inside string literals do not create phantom tuple elements.

`match` and `case` statements now have explicit `StmtKind` values. The parser
stores the match subject, case pattern text, optional guards, parsed subject
expressions, parsed pattern expressions for simple forms, and parsed guard
expressions. Pattern matching still stops in the unsupported-feature pass, so
the compiler cannot lower a half-implemented match.

`super.method(...)` semantic checks use the parsed call callee and argument
nodes. Valid single-base calls lower to explicit C++ base dispatch such as
`Base::method(args...)`; invalid `super` usage gets Dudu diagnostics instead of
falling through to generated C++.

`super.init(...)` semantic checks also use parsed call arguments. A valid
single-base constructor call must be the first statement in `init`; it validates
against the base constructor and emits through the C++ constructor initializer
list instead of lowering as a normal statement.

## Acceptance

- Existing examples still compile and run.
- Generated C++ remains readable.
- Parser errors point at exact tokens.
- Type errors point at exact expressions.
- LSP semantic tokens distinguish types, locals, fields, methods, params, and
  native symbols.
- `duc check` can diagnose common mistakes without relying on generated C++
  failure output.
- AST lowering covers the forms used by the generic and macro target examples.
