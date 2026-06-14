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
- source range fields on statement, expression, and type nodes
- semantic diagnostics for return values, local initializer values, local type
  names, conditions, and assignment targets use expression/type node locations
  where available
- initial `TypeRef` C++ lowering is implemented and used for type aliases
- template calls keep template arguments separate from runtime call arguments,
  and C++ emission lowers them from the parsed expression node
- array/list/dict index type inference uses parsed index expressions where
  available, so tuple-shaped multi-index expressions no longer depend on raw
  comma splitting
- native C/C++ overload checks for ordinary and explicit-template parsed calls
  consume expression children directly instead of flattening arguments back to
  strings
- parsed `new[T]` and `malloc[T]` allocation calls validate argument counts
  from expression children instead of stringified call arguments
- AST-backed statement checks route assignment compatibility through expression
  nodes instead of using raw statement value strings
- type compatibility has AST overloads for simple literals and list/set/dict
  literals, including expected-type disambiguation for empty `{}` dict
  initializers, reducing reliance on string parsing for assignment checks
- C++ assignment emission detects `Option` reset from `NoneLiteral` expression
  nodes instead of raw value text
- generated local C++ type inference has an AST path for names and calls,
  leaving raw text as a fallback for unlifted expression shapes
- C++ `if constexpr` detection for build-only conditions uses parsed condition
  expressions, with raw text scanning only as an unknown-expression fallback
- C++ expression emission writes string, integer, and float literal expression
  nodes directly, and lowers `str(value)` calls through an explicit AST path
  instead of sending them through raw expression lowering
- semantic checks for assignment through `*ptr` use parsed unary target
  expressions before falling back to raw target text
- semantic checks for plain-name and member assignment targets use parsed
  target expressions before falling back to raw target text for complex shapes
- semantic checks and C++ statement emission use expression-node presence for
  optional statement values/messages instead of raw statement value strings
- semantic compatibility checks for parsed binary and comparison operators use
  the right-hand expression node instead of stringifying it for assignment rules

Still too string-based:

- semantic analysis of local variable declarations
- semantic analysis of assignment and compound assignment
- semantic analysis of if, elif, else
- semantic analysis of while and for
- try, except
- C++ emission of calls, member access, operators, constructors, lambdas, and
  C++ macro calls
- semantic analysis and most C++ emission paths for type strings
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
now type-check parsed argument `Expr` nodes directly. Remaining call-related
string fallback is limited to unsupported expression shapes and the old raw
inference path.

C++ statement emission now lowers return values, assert/debug_assert
conditions, raise values, if/elif/while conditions, for iterables, and bare
expression statements from parsed `Expr` nodes where those nodes are
structurally reliable. Complex collection literals, lambdas, template calls,
native calls, and unsupported expression shapes still fall back to the existing
string lowering path.

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

The old raw-text compound-assignment normalization fallback has been removed.
Compound assignment is emitted only through the parsed `CompoundAssign` node;
the statement catch-all is now limited to `Unknown` statements.

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

Named-argument call C++ emission now emits from parsed call children. The raw
named-call rewrite remains only as a fallback for unknown/raw expression paths.
Named call arguments such as `Point(x=1)` now parse as `NamedArg` expression
nodes, and constructor semantic checks consume those nodes directly instead of
rediscovering named fields from raw argument strings.
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
