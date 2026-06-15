# Generics Plan

Dudu should support native compile-time generics with Python-shaped syntax and
C++ template lowering.

Imported C++ templates already work in useful cases. This plan is about writing
generic Dudu code directly.

## Syntax

Generic functions:

```python
def clamp[T](x: T, lo: T, hi: T) -> T:
    if x < lo:
        return lo
    if x > hi:
        return hi
    return x
```

Generic classes:

```python
class Box[T]:
    value: T

    def get(self) -> T:
        return self.value

    def set(self, value: T):
        self.value = value
```

Multiple type parameters:

```python
class Pair[A, B]:
    first: A
    second: B
```

Explicit instantiation at call sites:

```python
value: i32 = clamp[i32](42, 0, 100)
box: Box[f32] = Box[f32](1.5)
```

Type inference at call sites should work when every type parameter is determined
from arguments:

```python
value: i32 = clamp(42, 0, 100)
```

Explicit type arguments remain available when inference is unclear.

## Meaning

Generics are compile-time only.

- Type parameters do not exist at runtime.
- `T` is not a Python object.
- Dudu emits C++ templates or concrete generated instantiations.
- Generic code is checked against its syntax and instantiated uses.
- No trait system, concepts, or where-clauses are required for the first design.

Status: generic parameter syntax for Dudu-native classes and functions parses
into the AST. Generic type parameters are visible to declaration and body
checking, duplicate type parameters are diagnosed, and generic classes/functions
emit readable C++ `template <typename ...>` declarations. Explicit generic
class construction such as `Box[i32](42)` compiles through C++ template
lowering and type-checks constructor arguments against the instantiated class
fields or `init` signature. Explicit generic free-function calls such as
`identity[i32](42)` resolve through parsed template-call nodes, instantiate the
declared Dudu signature, and type-check parsed runtime arguments. Explicit
single-parameter generic methods such as `box.id[i32](42)` emit C++ method
templates and type-check parsed runtime arguments. Simple free-function
call-site type inference is implemented when every type parameter can be bound
from argument types, including nested forms such as `list[T]`. Richer
instantiated diagnostics, operator-constrained generic bodies, broader generic
methods, and non-type template parameters remain. Multi-parameter generic
functions and classes such as `Pair[str, i32]` substitute receiver member types
through the declared class generic parameter names.

Unsupported operations on `T` should produce useful diagnostics that mention:

- the generic declaration
- the operation that failed
- the concrete instantiation that caused it

## Lowering

Direct C++ template lowering:

```python
class Box[T]:
    value: T
```

becomes:

```cpp
template <typename T>
struct Box {
    T value{};
};
```

Generic functions lower similarly:

```cpp
template <typename T>
T clamp(T x, T lo, T hi) {
    ...
}
```

Generated headers should expose Dudu generic declarations as C++ templates when
the target is C++ interop.

## Type Parameters

Rules:

- Type parameters use PascalCase single names by convention: `T`, `U`, `Value`,
  `Key`.
- Type parameters are scoped to the function or class declaration.
- Class type parameters are visible in methods.
- Method-level type parameters may be added to generic and non-generic classes.
- Reusing a type parameter name in an inner generic declaration is an error.

Examples:

```python
class Cache[Key, Value]:
    values: dict[Key, Value]

    def get(self, key: Key) -> Option[Value]:
        return self.values.get(key)

    def map[Out](self, fn: fn(Value) -> Out) -> list[Out]:
        out: list[Out] = []
        for item: Value in self.values.values():
            out.append(fn(item))
        return out
```

## Target Examples

These examples are the suite we should make compile and run.

### Clamp

```python
def clamp[T](x: T, lo: T, hi: T) -> T:
    if x < lo:
        return lo
    if x > hi:
        return hi
    return x

def main() -> i32:
    a: i32 = clamp(20, 0, 10)
    b: f32 = clamp[f32](0.5, 0.0, 1.0)
    return a - 10
```

### Box

```python
class Box[T]:
    value: T

    def get(self) -> T:
        return self.value

    def set(self, value: T):
        self.value = value
```

### Vec2

```python
class Vec2[T]:
    x: T
    y: T

    def add(self, other: Vec2[T]) -> Vec2[T]:
        return Vec2[T](self.x + other.x, self.y + other.y)

    def dot(self, other: Vec2[T]) -> T:
        return self.x * other.x + self.y * other.y
```

This should work for `i32`, `f32`, `f64`, and imported numeric wrapper types
that support the operators.

### Stack

```python
class Stack[T]:
    items: list[T]

    def push(self, value: T):
        self.items.append(value)

    def pop(self) -> Option[T]:
        if len(self.items) == 0:
            return None
        value: T = self.items[-1]
        self.items.pop()
        return value
```

### Arena Handle

```python
class Handle[T]:
    index: u32
    generation: u32

class Arena[T]:
    items: list[T]

    def get(self, handle: Handle[T]) -> &T:
        return self.items[handle.index]
```

This is a real game/dev-tools pattern.

### Result Helpers

```python
def unwrap_or[T, E](result: Result[T, E], fallback: T) -> T:
    if result.ok:
        return result.value
    return fallback
```

### Generic Sort Wrapper

```python
def sort_by[T](values: &list[T], cmp: fn(T, T) -> bool):
    std.sort(values.begin(), values.end(), cmp)
```

This stresses imported C++ algorithm interop.

### Generic Slice Math

```python
def sum[T](values: span[T]) -> T:
    total: T = T()
    for value: T in values:
        total += value
    return total
```

This stresses default construction, imported span-like types, iteration, and
operator use.

### Fixed Vector

```python
class SmallVec[T, N]:
    items: T[N]
    count: usize
```

Non-type template parameters such as `N` are useful for fixed-capacity
containers, math, graphics, and embedded code. Dudu should support them after
type-only generics are stable.

## Diagnostics

Required generic diagnostics:

- unknown type parameter
- duplicate type parameter
- missing template argument
- too many template arguments
- type parameter used as a value
- value parameter used as a type
- cannot infer type parameter
- conflicting inferred type parameter
- operation unsupported for instantiated type

Example:

```text
src/math.dd:4:16: error[dudu/generic-op]: operator + is not available for Player
        return a + b
               ^^^^^
note: while instantiating add[Player]
    add(player_a, player_b)
    ^^^
```

## AST Requirements

This feature depends on [AST Plan](ast-plan.md).

The compiler needs structured nodes for:

- type parameter declarations
- template argument lists
- generic function declarations
- generic class declarations
- generic method declarations
- generic call expressions
- generic constructor expressions
- instantiated signatures

## Acceptance

- Target examples compile and run.
- Generic classes emit readable C++ templates.
- Generic functions emit readable C++ templates.
- Generated headers expose C++-usable templates.
- Type inference works for simple function calls.
- Explicit type args work for functions, methods, constructors, and imported
  native templates.
- Errors point to Dudu source and mention the instantiation site.
