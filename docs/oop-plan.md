# OOP Plan

Dudu should support the useful Python-looking parts of object-oriented
programming that lower cleanly to C++. It should avoid Python's dynamic object
model and avoid importing C++'s most confusing class baggage into Dudu-native
code.

## Goals

- Classes with typed fields and methods.
- Python-style `self`.
- Constructors that look like normal calls.
- Destructors through RAII.
- Class-scoped functions, class constants, and explicit static fields.
- Operator overloads where they map cleanly to C++.
- C++ class interop, including inherited C++ methods.
- Minimal native inheritance unless real examples require it.

## Python Has

Python OOP includes:

- classes
- instance methods with explicit `self`
- `__init__`
- `__del__`
- class variables
- `@staticmethod`
- `@classmethod`
- properties
- inheritance
- multiple inheritance
- dynamic attributes
- monkeypatching
- metaclasses/descriptors

Dudu should keep the readable pieces and reject the dynamic runtime pieces.

## C++ Has

C++ OOP includes:

- structs/classes
- public/private/protected members
- constructors/destructors
- overloaded constructors
- copy/move constructors and assignment
- static members
- operator overloads
- inheritance
- virtual methods
- templates
- RAII

Dudu should expose the parts that make systems code nicer and consume the rest
through native interop when imported C++ APIs require it.

## Classes

Dudu-native classes are value types by default and lower to C++ struct-like
types:

```python
class Vec2:
    x: f32
    y: f32
```

Fields are declared with annotations. No dynamic fields:

```python
v = Vec2(1.0, 2.0)
v.z = 3.0  # error: unknown field
```

## Methods

Methods use Python's explicit `self`:

```python
class Player:
    hp: i32

    def damage(self, amount: i32):
        self.hp -= amount

    def alive(self) -> bool:
        return self.hp > 0
```

Out-of-line methods can support C++-style organization:

```python
def Player.damage(self, amount: i32):
    self.hp -= amount
```

Status: implemented for methods attached to classes already declared in the
same module. The dotted `def Player.damage(...)` is parsed as a real method on
`Player`, so `self` gets the same implicit receiver type as an in-class method
and sema/emission use the normal method path.

Functions inside a class that do not take `self` are class-scoped functions.
They lower to C++ static member functions:

```python
class Math:
    def clamp(x: f32, lo: f32, hi: f32) -> f32:
        if x < lo:
            return lo
        if x > hi:
            return hi
        return x
```

No implicit instance is passed. If a class function reads instance fields
without `self`, the compiler should diagnose that and suggest adding `self` as
the first parameter.

## Constructors

Default construction:

```python
pos = Vec2()
```

Positional construction:

```python
pos = Vec2(10.0, 20.0)
```

Named field construction:

```python
pos = Vec2(x=10.0, y=20.0)
```

Custom constructor syntax uses the reserved class method name `init`:

```python
class Player:
    hp: i32
    name: str

    def init(self, hp: i32, name: str):
        self.hp = hp
        self.name = name
```

This lowers to a C++ constructor. `init` is not a normal callable method.
Heap allocation stays explicit with `new[T](...)`; `init` only describes how a
value is constructed.

Status: implemented. `init` is the only constructor spelling; Python dunder
aliases are not part of Dudu's class surface.

## Destructors And RAII

Dudu should use RAII as the cleanup model.

```python
class File:
    handle: *FILE

    def drop(self):
        if self.handle != None:
            c.fclose(self.handle)
```

Objects clean up at lexical scope exit or when their owning container destroys
them. Dudu should not add `defer` as the normal cleanup path.

For imported C++ classes, destructors run according to normal C++ lifetime
rules.

`drop` lowers to a C++ destructor. It is not called manually by normal Dudu
code. Manually owned heap objects use the allocation helpers documented in the
appearance spec, such as `delete(ptr)` for memory created by `new[T](...)`.

Status: implemented. `drop` is the only destructor spelling; Python dunder
aliases are not part of Dudu's class surface.

## Class Constants And Static Fields

Annotated class body bindings are instance fields by default, including fields
with defaults:

```python
class Player:
    hp: i32
    speed: f32 = 240.0
```

Class-scoped `ALL_CAPS` bindings are constants:

```python
class Color:
    WHITE: Color = Color(1.0, 1.0, 1.0, 1.0)
```

Mutable class-shared fields must be explicit with `static[T]`:

```python
class Counter:
    count: static[i32] = 0

    def bump() -> i32:
        Counter.count += 1
        return Counter.count
```

This maps to C++ static data members: one value shared by the class, not one
value stored in each instance.

Status: instance fields with default values and explicit `static[T]` class
fields are implemented. Functions inside classes with no `self` lower as static
member functions without requiring `@staticmethod`.

Static fields must be accessed with the type-qualified name:

```python
Counter.count += 1
```

Inside class methods, `class.name` is accepted as a current-class shorthand:

```python
class Counter:
    count: static[i32] = 0

    def bump() -> i32:
        class.count += 1
        return class.count
```

Do not access static fields through `self`. `self.count` always means an
instance field named `count`.

## Class Methods

Python has `@staticmethod` and `@classmethod`, but Dudu does not use those
decorators for the core object model. A class-scoped function is just a function
inside a class with no `self` parameter:

```python
class Color:
    def rgb(r: f32, g: f32, b: f32) -> Color:
        return Color(r, g, b, 1.0)
```

Keep `@classmethod` out of core unless concrete C++ interop or generic class
object examples require it.

`@staticmethod` is not accepted as a compatibility alias. Leaving off `self` is
the spelling.

Status: `@staticmethod`, `@classmethod`, and `@property` are rejected with
explicit OOP-surface diagnostics instead of generic unknown-decorator errors.

## Visibility

Use Python naming convention:

- `name` is public/importable.
- `_name` is private/internal.
- `_` alone is an ignored binding.

No `public` or `private` keyword for normal Dudu-native code.
PascalCase type names are ordinary C++ API types. Explicit type export controls
belong with the C ABI/shared-library visibility policy, not the normal class
syntax.

```python
class Player:
    hp: i32
    _invuln_timer: f32

def update_player(player: &Player):
    _tick_invuln(player)

def _tick_invuln(player: &Player):
    ...
```

For functions and methods, leading underscore privacy affects generated headers,
imports, LSP completion, and diagnostics. It is not a runtime access-control
system.

## Properties

Do not add Dudu-native properties. They make reads look like field access while
potentially running arbitrary code, which fights Dudu's goal of obvious native
costs and side effects.

Use explicit methods:

```python
player.health_percent()
```

Imported C++ APIs that expose getter/setter methods should remain method calls
in Dudu:

```python
class Player:
    hp: i32
    max_hp: i32

    def health_percent(self) -> f32:
        return f32(self.hp) / f32(self.max_hp)
```

## Operator Overloads

Dudu-native operator overloads use explicit operator decorators. Dudu reserves
Python-style `__name__` protocol names instead of translating them. Dunder
names are rejected on Dudu-defined methods and free functions. This keeps the
native surface small and gives Dudu room for operators Python does not spell
cleanly:

```python
class Vec2:
    x: f32
    y: f32

    @operator("+")
    def add(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)

    @operator("==")
    def equals(self, other: Vec2) -> bool:
        return self.x == other.x and self.y == other.y
```

Supported first set:

- `@operator("+")`
- `@operator("-")`
- `@operator("*")`
- `@operator("/")`
- `@operator("%")`
- `@operator("==")`
- `@operator("!=")`
- `@operator("<")`
- `@operator("<=")`
- `@operator(">")`
- `@operator(">=")`
- `@operator("bool")`

Indexing operators belong with the array/tensor plan:

- `@operator("[]")`
- `@operator("[]=")`

Status: implemented for the first operator set. `@operator("[]")` read hooks
and `@operator("[]=")` indexed assignment hooks are implemented for
library-style tensor wrappers.

## Inheritance

Dudu-native inheritance is planned separately and should be implemented against
the real AST/type model, not as a string-lowering patch. Composition and
generics cover many Dudu-native examples, but imported C++ and some native
systems patterns need a deliberate inheritance surface.

```python
class Player:
    transform: Transform
    health: Health
```

Imported C++ inheritance must work well enough to consume real C++ APIs:

- construct derived C++ classes
- call inherited methods
- pass derived/base pointers and references
- resolve inherited overloads

Dudu-native inheritance follows the inheritance plan: method-only `@abstract`,
`@override`, `@virtual`, `super`, strict multiple inheritance, and
interface-like abstract classes.

## Virtual Methods

Do not add Dudu-native virtual methods as a narrow OOP patch. Implement them as
part of the native inheritance plan.

C++ virtual dispatch remains available through imported C++ classes. Dudu can
call virtual methods on imported objects because the generated C++ uses normal
C++ calls.

## Copy And Move

Dudu follows C++ value semantics:

- copy if the type is copyable
- move with `move(value)`
- error if a noncopyable type is copied
- destructors run through normal C++ lifetime

```python
next = move(current)
```

The compiler should give Dudu-source diagnostics for copy/move failures where
possible.

## Dynamic Features Rejected

Dudu-native classes should not support:

- monkeypatching
- dynamic field creation
- metaclasses
- descriptors
- runtime class creation
- implicit `__dict__`

These features are powerful in Python, but they fight native layout, generated
C++, predictable performance, and editor tooling.

## Acceptance

- Classes with fields/methods compile and run.
- `init` and `drop` lower to constructors/destructors.
- Class-scoped functions, class constants, and explicit static fields work.
- `_name` visibility affects generated/imported public surface.
- `@operator(...)` overloads lower to C++ operators.
- Imported C++ inheritance remains consumable.
- Dudu-native inheritance follows the separate inheritance plan.
