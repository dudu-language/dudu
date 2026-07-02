# Inheritance Plan

Dudu should be able to consume C++ inheritance cleanly and should have a native
inheritance story that feels Python-shaped while lowering predictably to C++.

Native inheritance is not needed for every Dudu program, but users will expect
it for GUI frameworks, engines, plugin APIs, abstract interfaces, and imported
C++ ecosystems.

Another legitimate use case is graph-shaped runtime systems such as autograd
expression trees. A tensor library may want a common base node with many
operation-specific derived nodes, parent/child links, and virtual backward
behavior. That is not a great fit for a closed sum type when users and libraries
need to add new operation nodes.

## Goals

- Single inheritance for Dudu-native concrete classes.
- Abstract base classes.
- Interface-like abstract classes.
- Override checking.
- `super` calls.
- Current-class static access through `class.name`.
- C++ interop for inherited imported types.
- Multiple inheritance only with strict rules.
- Diagnostics that explain layout, dispatch, and ambiguity in Dudu terms.

## Non-Goals

Dudu should not copy Python's dynamic inheritance model:

- no monkeypatching base classes
- no runtime method injection
- no metaclasses
- no descriptor protocol
- no dynamic method resolution order changes
- no implicit field creation through base classes

Dudu should also avoid exposing the most fragile C++ inheritance patterns as
the normal native style.

## Basic Inheritance

Use Python base-list syntax:

```python
class Entity:
    id: u64

    def update(self, dt: f32):
        pass


class Player(Entity):
    hp: i32

    def update(self, dt: f32):
        super.update(dt)
        self.hp += 0
```

Lowering:

```cpp
struct Entity {
    uint64_t id;
    void update(float dt);
};

struct Player : Entity {
    int32_t hp;
    void update(float dt);
};
```

Native single inheritance should require that the base layout is known.

Status: Dudu parses Python-style base lists, validates base type names and
duplicate bases, emits native classes as C++ public inheritance, orders base
classes before derived classes in generated C++, and resolves inherited fields
and methods for normal member access. Base constructor calls through
`super.init(...)`, `super.method(...)`, virtual/abstract/override checking,
strict multiple inheritance, virtual destructor policy, generic native bases,
and current-class static access are implemented in the later sections below.

## Constructors

Constructors use `init`. Base construction is explicit with `super.init(...)`:

```python
class Entity:
    id: u64

    def init(self, id: u64):
        self.id = id


class Player(Entity):
    hp: i32

    def init(self, id: u64, hp: i32):
        super.init(id)
        self.hp = hp
```

The compiler lowers `super.init(...)` to a C++ base constructor call where C++
requires one.

Status: `super.init(...)` is implemented for single-base constructors when it
is the first statement in `init`. It validates parsed arguments against the base
constructor and lowers to a C++ base initializer list entry.

## Destructors

Destructors use `drop`. Base cleanup follows C++ destruction order. User code
does not call `super.drop()` unless an imported C++ API explicitly requires a
normal method call with that name.

```python
class FileBacked:
    file: *FILE

    def drop(self):
        if self.file != None:
            c.fclose(self.file)
```

If a class is used through a base pointer and has virtual methods, generated C++
must provide a virtual destructor or diagnose that deletion through the base type
is unsafe.

Status: classes with virtual or abstract instance methods, plus classes that
derive from them, emit virtual destructors automatically. A user `drop` method
lowers to a virtual destructor for those classes; otherwise a default virtual
destructor is emitted.

## Overrides

Use an explicit decorator for override checking:

```python
class Player(Entity):
    @override
    def update(self, dt: f32):
        super.update(dt)
```

`@override` lowers to C++ `override` when the method is virtual. If there is no
matching base method, it is an error.

For methods that intentionally introduce a new method, omit `@override`.

Status: method-level `@override` is implemented for Dudu-native bases. It
requires a matching base method, matching parameter and return types after
`self`, and a base target marked `@virtual` or `@abstract`.

## Virtual Methods

Use an explicit decorator when dynamic dispatch is wanted:

```python
class Entity:
    @virtual
    def update(self, dt: f32):
        pass
```

```python
class Player(Entity):
    @override
    def update(self, dt: f32):
        ...
```

Static dispatch remains the default. This keeps value-type systems code cheap
unless the author asks for a vtable.

Status: method-level `@virtual` is implemented for instance methods and lowers
to a C++ virtual method.

## Abstract Methods

Use `@abstract` on required methods. `@abstract` is method-only; classes become
abstract automatically when they declare or inherit abstract methods:

```python
class Shape:
    @abstract
    def area(self) -> f32
```

Classes with abstract methods cannot be constructed:

```python
shape = Shape()  # error
```

Lowering should use a pure virtual method:

```cpp
virtual float area() = 0;
```

`@abstract` implies `@virtual`.

`pass` is always a concrete no-op body, never an abstract marker:

```python
class DebugShape(Shape):
    @override
    def area(self) -> f32:
        pass  # concrete no-op body
```

A bodyless method without `@abstract` is an error:

```python
class Sprite:
    def draw(self, canvas: &Canvas)  # error: add @abstract or provide a body
```

Status: method-level `@abstract` is implemented for instance methods. It
rejects method bodies, implies virtual dispatch, emits a C++ pure virtual
method, and bodyless non-abstract methods are rejected. Explicit Dudu-source
checks reject construction and `new[T]` allocation of classes with
unimplemented abstract methods, including inherited abstract methods.

## Interface-Like Abstract Classes

Dudu should not add an `interface` keyword. Pure behavior contracts should use
normal Python-looking classes with abstract methods:

```python
class Drawable:
    @abstract
    def draw(self, canvas: &Canvas)


class Tickable:
    @abstract
    def tick(self, dt: f32)
```

An abstract class is interface-like when it has no instance fields and all
required behavior is abstract. It can be used as one of several bases without
contributing storage.

Implementation uses the same base-list syntax:

```python
class Sprite(Drawable, Tickable):
    texture: Texture

    @override
    def draw(self, canvas: &Canvas):
        canvas.draw_texture(self.texture)

    @override
    def tick(self, dt: f32):
        pass
```

Interface-like abstract classes lower to C++ abstract classes with pure virtual
methods. If multiple interface-like bases are used, they must not contribute
storage.

Status: strict native multiple inheritance is implemented for the common
one-storage-base plus interface-like abstract bases shape. Non-storage bases in
multi-base lists must be abstract/interface-like, multiple storage-bearing Dudu
bases are rejected, and duplicate inherited concrete methods are rejected unless
the derived class overrides the method.

## Default Methods On Interface-Like Bases

Default methods are allowed when they do not require fields:

```python
class Named:
    @abstract
    def name(self) -> str

    def display_name(self) -> str:
        return self.name()
```

This lowers like a normal C++ virtual/default method on an abstract base.

## Multiple Inheritance

Multiple inheritance should be strict:

```python
class Sprite(Node, Drawable, Tickable):
    ...
```

Rules:

- at most one concrete Dudu-native base with fields
- any number of interface-like abstract bases
- no two bases may provide the same non-abstract method unless the derived class
  overrides it
- no two bases may contribute fields with the same name
- diamond concrete inheritance is rejected for native Dudu classes

This supports the common C++ pattern:

```cpp
struct Sprite : Node, Drawable, Tickable {};
```

without making native Dudu layout ambiguous.

## Imported C++ Multiple Inheritance

Imported C++ classes may use multiple inheritance that Dudu would not allow
native code to define. Dudu must still consume those types:

```python
window: *QMainWindow = ...
window.show()
```

Header awareness should preserve:

- base class lists
- inherited methods
- pointer/reference conversions
- virtual methods
- overload sets across bases
- access diagnostics when a base member is protected/private

Dudu should not try to re-model every imported layout rule in user syntax. The
generated C++ remains the authority for imported C++ class ABI behavior.

## Access Control

Dudu-native visibility should keep using naming rules:

- `name` is public
- `_name` is internal/private
- `_` is ignored

Imported C++ `public`, `protected`, and `private` access must be respected by
the generated C++ and surfaced as diagnostics.

Do not add Dudu-native `public`, `protected`, or `private` keywords unless real
examples prove naming rules are insufficient.

## Static Members And Current Class

Static fields use `static[T]`:

```python
class Counter:
    count: static[i32] = 0

    def init(self):
        Counter.count += 1
```

Inside class scope, `class.name` is allowed as a current-class alias:

```python
class Counter:
    count: static[i32] = 0

    def init(self):
        class.count += 1
```

Rules:

- `self.name` means instance state or instance method
- `class.name` means current class static member
- `Type.name` means explicit type-qualified static/member access
- `super.name(...)` means explicit base-class method dispatch

Do not fall back from `self.name` to static members.

## Operator Overrides

Operator overloads use `@operator(...)`. Overrides of inherited operators still
use `@override`:

```python
class Meter(Scalar):
    @override
    @operator("+")
    def add(self, other: Meter) -> Meter:
        return Meter(self.value + other.value)
```

The compiler should reject decorator combinations that do not map to valid C++.

## Generics And Inheritance

Generic bases should use normal generic syntax:

```python
class Repository[T]:
    @abstract
    def get(self, id: u64) -> Option[T]


class PlayerRepository(Repository[Player]):
    @override
    def get(self, id: u64) -> Option[Player]:
        ...
```

The generics plan owns template instantiation rules. The inheritance checker
must see the instantiated base surface when validating overrides.

Status: generic Dudu-native bases such as `Repository[Player]` are supported
for override checking. The inherited base method signature is substituted
through the base type arguments before comparing against the derived method, so
`def get(...) -> Option[Player]` correctly overrides
`def get(...) -> Option[T]` from `Repository[T]`.

## Pattern Matching

Inheritance does not replace sum types. If the set of variants is known and
closed, prefer `enum` from the sum-types plan:

```python
enum Message:
    Quit
    Move:
        x: i32
        y: i32
```

Use inheritance for open extension points and imported C++ object models.

Autograd-style compute graphs are another open-extension case:

```python
class GradNode:
    children: list[*GradNode]

    @abstract
    def backward(self, grad: Tensor)


class AddNode(GradNode):
    left: *GradNode
    right: *GradNode

    @override
    def backward(self, grad: Tensor):
        self.left.backward(grad)
        self.right.backward(grad)
```

The concrete representation may use intrusive refs, arenas, or library-owned
handles instead of raw pointers, but the language should support the shape.

Status: an executable autograd-style graph fixture covers an abstract base
node, derived operation nodes, explicit base construction, derived-to-base raw
pointers, virtual dispatch through base pointers, and method calls through
pointer-typed member fields.

## Target Examples

These examples are the suite we should make compile and run as the feature
lands.

### Basic Base Class

```python
class Entity:
    id: u64

    def update(self, dt: f32):
        pass


class Player(Entity):
    hp: i32

    @override
    def update(self, dt: f32):
        super.update(dt)
```

### Abstract Contract

```python
class Drawable:
    @abstract
    def draw(self, canvas: &Canvas)


class Sprite(Drawable):
    @override
    def draw(self, canvas: &Canvas):
        canvas.draw_sprite(self)
```

### Partial Abstract Base

```python
class Tickable:
    @abstract
    def tick(self, dt: f32)

    def reset(self):
        pass
```

### Multiple Interface-Like Bases

```python
class Sprite(Node, Drawable, Tickable):
    @override
    def draw(self, canvas: &Canvas):
        ...

    @override
    def tick(self, dt: f32):
        ...
```

### Autograd Graph

```python
class GradNode:
    children: list[*GradNode]

    @abstract
    def backward(self, grad: Tensor)


class MulNode(GradNode):
    left: *GradNode
    right: *GradNode

    @override
    def backward(self, grad: Tensor):
        self.left.backward(grad)
        self.right.backward(grad)
```

### Imported C++ Base

```python
from cpp.path import widgets.hpp as widgets

class Button(widgets.Widget):
    @override
    def draw(self, canvas: &Canvas):
        super.draw(canvas)
```

Status: `tests/fixtures/native_imported_base.dd` covers a Dudu class deriving
from a C++ type imported through a non-namespace header alias. It validates
inherited method calls, base field access, and derived-to-base pointer handoff
without wrapper headers.

## Diagnostics

Required diagnostics:

- base class not found
- base class not constructible
- attempted construction of abstract class
- bodyless method without `@abstract`
- `@override` without matching base method
- override return type mismatch
- override argument mismatch
- missing implementation of abstract method
- multiple concrete bases with storage
- ambiguous inherited method
- static member accessed through `self`
- `super` used outside derived class method
- virtual destructor required for unsafe base deletion

Diagnostics should point at Dudu source and include the relevant base method or
field declaration where possible.

## Acceptance

- Native single inheritance compiles and runs: initial base-list parsing,
  inherited fields, inherited methods, and C++ public-base lowering are done.
- `super.init(...)` lowers correctly: done for single-base constructors.
- `super.method(...)` calls the base method: done for single-base classes.
- `@virtual`, method-only `@abstract`, and `@override` validate method
  relationships.
- Classes with unimplemented abstract methods cannot be constructed: done for
  Dudu constructor calls and `new[T]` allocation.
- Interface-like abstract classes compile to abstract C++ bases.
- Classes may implement multiple interface-like bases: done for fieldless
  abstract bases and one optional storage-bearing base.
- target examples exist as fixtures or runnable examples.
- Multiple concrete native bases are rejected: done for multiple
  storage-bearing Dudu bases.
- Virtual destructor policy is handled for Dudu-native polymorphic classes.
- Imported C++ inherited methods remain callable: covered by
  `native_imported_base`.
- Imported C++ derived/base pointer and reference conversions work: covered by
  `native_imported_base`.
- Generic Dudu-native base classes substitute type parameters during override
  checks.
- Static fields use `static[T]`, `class.name` works inside class scope, and
  `self.name` does not fall back to static fields.
