# Python Subset Compiler Plan

This is the implementation plan for the new Dudu goal:

```text
A statically typed subset of Python that compiles to readable C++,
has direct access to C and C++ libraries, and emits normal C++ headers
so Dudu code can be used from C++.
```

The older low-punctuation Dudu syntax is no longer the target.

## Current Inputs

Primary current design docs and fixtures:

- [Appearance Spec](appearance-spec.md)
- `examples/python_syntax/*.dd`

Historical docs that no longer describe the target syntax:

- [Language Sketch](language.md)
- [Compiler Plan](compiler-plan.md)
- [Interop Plan](interop.md)
- [Development Plan](development-plan.md)

Those older docs still contain useful implementation notes, but their syntax
examples should not drive the next compiler work.

## Product Goal

Dudu should feel like typed Python:

```python
def add(a: i32, b: i32) -> i32:
    return a + b
```

But compile like C++:

```cpp
int32_t add(int32_t a, int32_t b) {
    return a + b;
}
```

The project should avoid preserving Python runtime semantics when those
semantics fight C++ output. Dudu is Python-shaped, not CPython-compatible.

## Language Baseline

Implement this first:

- `.dd` files with Python-like indentation.
- Python comments.
- `import`, `import c "..." as name`, `import cpp "..." as name`.
- `class` with typed fields and methods.
- `enum`.
- `type Name = Type`.
- `def name(args...) -> Return:`.
- Python calls with parentheses and commas.
- `return`, `if`/`elif`/`else`, `while`, `for`, `break`, `continue`.
- typed local annotations: `x: i32 = 0`.
- local type inference from initializers.
- constants via mandatory `ALL_CAPS` bindings.
- tuple return and destructuring assignment.
- `Result[T, E]` and `Option[T]`.
- no exceptions in v1.
- typed loop variable extension:
  ```python
  for x: &Thing in things:
      ...
  ```
- no implicit casts across Dudu-native assignments and returns.

## Type Surface

Built-ins:

```text
bool
i8 i16 i32 i64
u8 u16 u32 u64
isize usize
f32 f64
void
str
cstr
```

Containers:

```python
values: list[i32]
names: dict[str, i32]
seen: set[str]
pair: tuple[i32, f32]
static_values: i32[256]
matrix: f32[4][4]
```

Native low-level types:

```python
p: *i32
r: &i32
pc: *const[i32]
rc: &const[PlayerState]
```

Imported C++ templates:

```python
items: std.vector[i32]
table: std.unordered_map[str, PlayerState]
ptr = std.make_unique[PlayerState](10, 20)
```

## Removed Python Semantics

Reject or delay:

- `raise`, `try`, `except`, `finally`
- `eval`, `exec`
- monkeypatching modules/classes/functions
- dynamic attribute creation on typed classes
- metaclasses and descriptors
- arbitrary `getattr`/`setattr` by runtime string
- rebinding a local to a different type
- arbitrary CPython package imports
- generators and `yield`
- `async` and `await`
- multiple inheritance
- runtime class creation
- decorators, except compiler-recognized ones later
- heterogeneous containers unless explicitly typed dynamic object support exists
- arbitrary precision Python `int` as the default integer model

## Compiler Architecture

Split the current compiler before adding the new parser.

Target layout:

```text
src/
    main.cpp
    diagnostics.hpp/.cpp
    source.hpp/.cpp
    lexer.hpp/.cpp
    tokens.hpp
    ast.hpp/.cpp
    parser.hpp/.cpp
    resolver.hpp/.cpp
    types.hpp/.cpp
    checker.hpp/.cpp
    cpp_emit.hpp/.cpp
```

Keep files near the repo's 300-500 line target where practical.

## Stage 1: Replace The Parser

Do not mutate the old line-oriented parser forever. Add a real lexer/parser for
the Python-shaped syntax.

Lexer requirements:

- indentation tokens: `indent`, `dedent`, `newline`
- names, keywords, string literals, numeric literals
- punctuation: `()[]{},.:`
- operators: `=`, `+=`, `-=`, `*=`, `/=`, comparisons, arithmetic, `&`, `*`
- comments and blank lines

Parser requirements:

- module imports
- foreign imports
- classes
- enums
- aliases
- functions
- statements
- expressions
- type expressions

Parsing target examples:

- `examples/python_syntax/numerics_kmeans.dd`
- `examples/python_syntax/cpp_library.dd`
- a tiny hello fixture

## Stage 2: AST And Type AST

Represent Python-shaped syntax directly.

Important AST forms:

- `Module`
- `Import`
- `ForeignImport`
- `ClassDecl`
- `EnumDecl`
- `TypeAlias`
- `FunctionDecl`
- `Block`
- `IfStmt`
- `WhileStmt`
- `ForStmt`
- `ReturnStmt`
- `AssignStmt`
- `DestructureAssignStmt`
- `ExprStmt`
- `CallExpr`
- `MemberExpr`
- `IndexExpr`
- `UnaryExpr`
- `BinaryExpr`
- `TupleExpr`
- `ListLiteral`
- `DictLiteral`
- `SetLiteral`

Type AST forms:

- named type: `i32`, `PlayerState`, `glm.vec3`
- generic type: `list[i32]`, `Result[T, E]`
- fixed array suffix: `i32[256]`
- pointer: `*T`
- reference: `&T`
- const: `const[T]`

## Stage 3: Minimal Typechecker

Typecheck Dudu-native code before serious interop.

Implement:

- scalar type lookup
- class type lookup
- enum type lookup
- type aliases
- function signatures
- local scopes
- Dudu-native naming rules, including all-caps constants
- fields and member access for Dudu classes
- function calls
- constructor calls
- basic operators
- list/dict/set/tuple literals where annotated
- tuple destructuring count/type checks
- typed `for` variables
- no implicit casts in Dudu-native assignments/returns
- `Result`/`Option` as known generic types

Do not attempt full C++ overload resolution yet.

## Stage 4: C++ Emission

Emit readable C++ first.

Mappings:

```text
i32                 -> int32_t
u64                 -> uint64_t
f32                 -> float
str                 -> std::string
list[T]             -> std::vector<T>
dict[K, V]          -> std::unordered_map<K, V>
set[T]              -> std::unordered_set<T>
tuple[...]          -> generated aggregate by default
T[N]                -> std::array<T, N>
*T                  -> T*
&T                  -> T&
const[T]            -> const T
Result[T, E]        -> dudu::Result<T, E>
Option[T]           -> std::optional<T>
```

Emit:

- `.cpp` for executable targets
- `.hpp` plus `.cpp` for library targets
- top-level functions as normal C++ functions
- classes as C++ structs/classes with public fields initially
- tuple returns as generated aggregate structs
- destructuring as C++ structured bindings

Example:

```python
def divmod_i32(a: i32, b: i32) -> tuple[i32, i32]:
    return a / b, a % b
```

Should emit something like:

```cpp
struct divmod_i32_result {
    int32_t _0;
    int32_t _1;
};

divmod_i32_result divmod_i32(int32_t a, int32_t b);
```

## Stage 5: Runtime Prelude

Add a tiny C++ prelude for Dudu-generated code.

Needed early:

- `dudu::Result<T, E>`
- `Ok(value)`
- `Err(error)`
- perhaps helper casts or checked conversion utilities

Prefer header-only at first:

```text
runtime/dudu.hpp
```

Keep it small and inspectable.

## Stage 6: C Interop

Emit-only interop first:

```python
import c "stdio.h" as c
```

Generated C++ includes the header and lowers dot paths:

```cpp
#include <stdio.h>
c.printf(...)
```

For C libraries, dot aliasing may require wrapper namespaces or direct textual
lowering. The first implementation can support:

- include emission
- alias-to-global lowering for C functions/constants
- pointer arguments
- simple structs/types if users spell imported type names

Add local fixtures:

```text
tests/fixtures/interop_c/
    dudu_c_math.h
    dudu_c_math.c
    main.dd
```

## Stage 7: C++ Interop

Emit-only C++ interop first:

```python
import cpp "glm/glm.hpp" as glm
import cpp "memory" as std
```

Generated C++ includes headers and lowers dot paths to C++ namespaces/members.

Support in first pass:

- namespaces
- free function calls
- constructors
- public fields
- member calls
- imported template spelling `std.vector[i32]`
- operator expressions that C++ can compile

Use the C++ compiler as the real validator until Clang import lands.

Add local C++ fixtures:

```text
tests/fixtures/interop_cpp/
    dudu_cpp_vec.hpp
    dudu_cpp_vec.cpp
    main.dd
```

## Stage 8: Clang-Backed Import

Full C++ library access eventually needs Clang tooling.

Do not hand-parse C++ headers.

Use Clang/libclang/clang tooling to inspect:

- functions
- overload sets
- namespaces
- records/classes
- methods
- constructors
- enums
- typedefs/using aliases
- templates
- constants and simple macros

This stage enables better diagnostics and Dudu-side overload resolution. Before
this stage, compile generated C++ and surface compiler errors clearly.

## Stage 9: Build Tool

Rename the compiler command to `duc`.

Expected commands:

```sh
duc emit src/main.dd -o build/main.cpp
duc build
duc run
duc test
duc fmt
duc src/main.dd --emit-cpp -
```

Use normal C/C++ build tools:

- direct `clang++`/`c++` compile for simple files
- CMake integration for projects
- `pkg-config` probes for common native libraries
- preserve `compile_commands.json`

## Stage 10: Formatter

`duc fmt` should behave like a Dudu-aware `black`.

Format:

- indentation
- blank lines
- operator spacing
- trailing commas in multiline calls/literals
- import ordering
- final newline

Warn/error on naming for Dudu-native declarations:

- types: `PascalCase`
- constants: `ALL_CAPS`
- functions, locals, fields: `snake_case`
- imported names: preserved

## Stage 11: Tests

Keep old compiler tests temporarily, but new work needs separate Python-syntax
fixtures.

Recommended layout:

```text
tests/
    python_syntax/
        basic/
        classes/
        functions/
        loops/
        tuples/
        results/
        containers/
        pointers/
        interop_c/
        interop_cpp/
    expected_cpp/
```

Test modes:

- parse-only
- typecheck-only
- emit C++ and diff important snippets
- compile generated C++
- run generated binary
- optional external library probes

Optional probes:

- GLM
- raylib
- SQLite
- OpenCV

Skip optional probes when dependencies are missing.

## Stage 12: Migration Strategy

The repo currently has an old compiler for old syntax.

Practical path:

1. Preserve old examples as legacy fixtures.
2. Add `examples/python_syntax` as the new design fixture set.
3. Split compiler source.
4. Add parser mode for new syntax.
5. Point tests at tiny new syntax fixtures first.
6. Once new syntax can build real examples, delete or archive the old syntax
   implementation.

Do not try to support both language syntaxes long-term.

## First Goal Run

A good `/goal` objective:

```text
Implement the first Dudu Python-subset compiler slice: split the compiler,
parse a tiny typed-Python `.dd` program, emit readable C++, compile it, and add
tests for functions, classes, locals, returns, and one tuple return.
```

Concrete first slice:

Input:

```python
class Vec2:
    x: f32
    y: f32


def add(a: i32, b: i32) -> i32:
    return a + b


def divmod_i32(a: i32, b: i32) -> tuple[i32, i32]:
    return a / b, a % b


def main() -> i32:
    q, r = divmod_i32(10, 3)
    return add(q, r)
```

Expected:

- parser accepts it
- typechecker validates it
- emitter generates C++ with a `Vec2` struct
- tuple return lowers to generated aggregate
- destructuring lowers to structured binding
- generated C++ compiles and runs

## Risks

- Python syntax is larger than the old Dudu syntax.
- Full C++ interop needs Clang tooling eventually.
- `*T` and `&T` are not Python syntax, so parser reuse is limited there.
- Dot lowering between namespace/member/pointer access requires type knowledge.
- Good diagnostics matter because users will expect Python-like clarity.

These risks are manageable if the first goal stays narrow.
