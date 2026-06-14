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
- nested raw statement blocks
- statement classification into `Stmt` nodes
- initial expression shape capture for common names, literals, calls,
  template calls, member/index access, unary/binary operators, conditionals,
  and collection literals
- initial type shape capture for names, qualified names, templates, pointers,
  references, wrappers, fixed arrays, and function-like type signatures
- source range fields on statement, expression, and type nodes

Still too string-based:

- semantic analysis of local variable declarations
- semantic analysis of assignment and compound assignment
- semantic analysis of if, elif, else
- semantic analysis of while and for
- try, except
- C++ emission of return/assert/debug_assert and expression statements
- C++ emission of calls, member access, indexing, operators, constructors,
  template calls, lambdas, tuple destructuring, and C++ macro calls
- semantic analysis and C++ emission of type strings
- exact original-token ranges inside function bodies; current body-node ranges
  are derived from normalized joined statement text

## Target Architecture

Keep the outer `ModuleAst`, but replace `RawStmt` with real statements and
expressions.

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

1. Add AST node types alongside `RawStmt`.
2. Parse simple statements into AST while preserving old lowering for unsupported
   forms.
3. Parse expressions into AST with source ranges.
4. Lower typed AST statements to C++.
5. Move semantic checks from string helpers into AST visitors.
6. Convert formatter and LSP to use AST nodes.
7. Remove raw statement lowering for normal Dudu syntax.
8. Keep `cpp(...)` as the explicit raw C++ escape hatch.

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
