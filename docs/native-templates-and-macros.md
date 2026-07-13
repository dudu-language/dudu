<a id="native-templates-and-macros"></a>
# Generics, Native Templates, And Macros

[Dudu manual](https://dudulang.org/docs.html#cpp-interop) | Previous: [Import semantics](import_semantics.md) | Next: [Allocation and lifetimes](allocation-and-lifetimes.md)

Four separate mechanisms use square brackets or call-shaped syntax in Dudu.
They are not interchangeable.

## Dudu Generics

Dudu functions and classes declare type parameters directly:

```python
def choose[T](left: T, right: T, take_right: bool) -> T:
    if take_right:
        return right
    return left

class Box[T]:
    value: T

number = choose(10, 20, true)
box = Box[i32](42)
```

Type arguments are inferred when arguments or expected return context determine
them. See [Generics](generics.md) for the complete inference rules.

## Dudu Value Generics

Generic parameters may also represent compile-time values. Shapes and fixed
capacities use this directly:

```python
def first[T, N](values: &array[T][N]) -> T:
    return values[0]

def valid_conv_height[H, K](image: &array[f32][H], kernel: &array[f32][K])
        -> array[f32][H - K + 1]:
    out: array[f32][H - K + 1]
    return out
```

`N`, `H`, and `K` are values in type expressions. They are not runtime function
arguments. Arithmetic in shaped return types is checked and folded during
concrete instantiation.

## Imported C++ Templates

An imported C++ template remains a native C++ template. Dudu reads its metadata
from Clang and emits the original C++ identity:

```python
from cpp import array
from cpp import memory
from cpp import string
from cpp import vector

bytes: std.array[u8, 16]
names: std.vector[std.string]
node = std.make_unique[Node]()
```

Native function templates can be called explicitly or inferred when the header
provides enough metadata:

```python
from cpp.path import vendor/math.hpp

same = vendor.identity[i32](41)
larger = vendor.max_value(10, 20)
```

Class templates, function templates, member templates, overload sets, type
parameters, non-type parameters, and common variadic packs are scanner-backed
native behavior. Dudu does not reimplement a named standard-library template as
a compiler special case.

## Imported C And C++ Macros

Object-like macros become native constants when the scanner can model their
value. Function-like macros remain calls and are emitted with their original
native spelling:

```python
from cpp.path import ../tests/fixtures/cpp_macro_bomb.hpp as macros

base: i32 = macros.DUDU_MACRO_BOMB_VALUE
scaled = macros.DUDU_MACRO_BOMB_SCALE(base)
first = macros.DUDU_MACRO_BOMB_FIRST(7, 8, 9)
```

The three lines exercise an object-like constant, a fixed-arity function-like
macro, and a variadic macro. Variadic macros validate their fixed leading
arguments and pass remaining arguments to the native preprocessor. Callable
lowercase macros such as C's `assert(expr)` are supported when the scanner
reports them as public macros.

The compatibility fixture is
[`tests/fixtures/cpp_macro_bomb.dd`](../tests/fixtures/cpp_macro_bomb.dd), backed
by
[`tests/fixtures/cpp_macro_bomb.hpp`](../tests/fixtures/cpp_macro_bomb.hpp).
It covers constants, fixed and variadic calls, statement macros, mutation,
lowercase names, stringizing, and token-pasting declarations in the imported
header.

## Native Macro Boundary

Macros that produce a complete value, expression, call, or statement are the
reliable direct-import case. The following can require a small native wrapper
header because their result is not independently valid Dudu syntax:

- token-pasting that manufactures a declaration or identifier for later Dudu
  source to name
- stringizing that depends on raw Dudu tokens rather than ordinary call
  arguments
- declaration-generating macros
- macros that expand to only part of a declaration, type, expression, or
  control-flow construct

The wrapper stays at the native boundary and exposes a normal function, type,
constant, or complete callable macro. This is a C/C++ preprocessor boundary,
not a second Dudu macro language.

Dudu-defined declaration, decorator, and expression macros are design work in
[Macro Syntax Plan](macro-syntax-plan.md). Compiler-known decorators such as
`@operator` and `@constexpr` are language features, not user-defined macros.

## Editor Behavior

When Clang provides source metadata, imported templates and macros participate
in the same editor operations as other native declarations:

- hover reports native identity, signature, documentation, and template
  parameters
- completion includes imported names and members
- signature help reports fixed parameters and variadic boundaries
- go-to-definition opens the declaration or macro definition in its header

Missing source metadata limits navigation, but it does not change generated
code or invent a Dudu-specific replacement declaration.

## Limits

- Dudu does not parse or emulate the complete C++ template language.
- Callable macros must expand to complete expressions that Clang can describe.
- Token pasting, stringizing, declaration generation, and partial-syntax
  macros require an ordinary native wrapper when they cannot be imported as a
  complete declaration or expression.
- User-defined Dudu macros are not part of the current language.

## Tested Examples

- [`native_template_function.dd`](../tests/fixtures/native_template_function.dd)
  checks explicit imported function-template arguments.
- [`cpp_template_member.dd`](../tests/fixtures/cpp_template_member.dd) checks
  imported class and member templates.
- [`cpp_macro_bomb.dd`](../tests/fixtures/cpp_macro_bomb.dd) checks constants,
  lowercase names, variadics, and expression macros.
- [`bad_native_template_function.dd`](../tests/fixtures/bad_native_template_function.dd)
  and [`bad_variadic_macro_arity.dd`](../tests/fixtures/bad_variadic_macro_arity.dd)
  verify native-boundary diagnostics.
