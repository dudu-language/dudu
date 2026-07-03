# Protocols And Serde Design Notes

These are design notes from the abstract-class, trait, interface, and Serde
discussion. This file is not a committed language feature list. It records the
problem shapes we want Dudu to keep in mind while the OOP, macro, and generic
systems mature.

## Core Tension

Dudu wants to feel like Python while compiling to ordinary C++ and consuming
ordinary C++ libraries. That creates two competing pressures:

- Python users expect classes, inheritance, decorators, and readable adapters.
- Systems users expect explicit conformance, predictable layout, fast dispatch,
  and no accidental magic.

The simplest useful center is:

- use abstract classes as Python-looking protocols/interfaces
- keep normal inheritance for owned types
- add an external conformance shape only when a third-party type must satisfy a
  protocol without changing that type
- keep Serde-like behavior as a library plus macro/decorator system, not a
  compiler builtin

## Abstract Classes As Protocols

An abstract class can describe a behavior contract:

```python
class Serialize:
    @abstract
    def serialize(self, s: &Serializer):
        pass

class Deserialize:
    @abstract
    def deserialize(d: &Deserializer) -> Result[Self, Error]:
        pass
```

An owned type can satisfy that contract with normal Python-shaped inheritance:

```python
class Player(Serialize):
    hp: i32
    name: str

    def serialize(self, s: &Serializer):
        s.field("hp", self.hp)
        s.field("name", self.name)
```

This keeps the common case simple. The class declaration says the type is a
`Serialize`, and normal method override checks can prove the contract is
implemented.

## Nominal, Not Structural By Default

Go-style structural interfaces are attractive because a type satisfies an
interface if it has the right methods. That is convenient, but it also makes
conformance accidental and harder to reason about during native interop.

Dudu should prefer explicit conformance:

```python
class Player(Serialize):
    ...
```

A class that merely has a method named `serialize` should not automatically
become a `Serialize`. A library may provide opt-in structural helpers, but the
language default should keep conformance visible.

## Inheritance Versus External Conformance

Inheritance and external conformance should not mean the same thing.

Inheritance is subtype/layout/runtime behavior:

```python
class Player(Serialize):
    ...

items: list[*Serialize] = []
items.append(&player)
```

External conformance is static adapter behavior for types we do not own:

```python
impl Serialize for ThirdPartyString:
    def serialize(value: &ThirdPartyString, s: &Serializer):
        s.string(value.c_str())
```

The second form should not imply that `ThirdPartyString` has a `Serialize` base
subobject or can be stored directly in `list[*Serialize]`. It means generic code
that asks for `T` satisfying `Serialize` can find the adapter.

```python
def write_json[T: Serialize](value: &T, out: &Writer):
    serializer = Serializer(out)
    value.serialize(serializer)
```

Possible lowering model:

- owned inheritance can lower to C++ inheritance or virtual dispatch where
  needed
- external conformance can lower to a generated/specialized adapter such as
  `SerializeImpl<ThirdPartyString>`
- generic calls select either the real member implementation or the external
  adapter during semantic analysis

## External Conformance Syntax Candidates

The Rust-shaped option is the clearest conceptually:

```python
impl Serialize for ThirdPartyString:
    def serialize(value: &ThirdPartyString, s: &Serializer):
        ...
```

It is also less Python-looking than the rest of Dudu.

Python-shaped candidates that keep `ThirdPartyString(Serialize)` visible:

```python
extend ThirdPartyString(Serialize):
    def serialize(value: &ThirdPartyString, s: &Serializer):
        ...

adapt ThirdPartyString(Serialize):
    def serialize(value: &ThirdPartyString, s: &Serializer):
        ...

conform ThirdPartyString(Serialize):
    def serialize(value: &ThirdPartyString, s: &Serializer):
        ...

implement ThirdPartyString(Serialize):
    def serialize(value: &ThirdPartyString, s: &Serializer):
        ...

reopen ThirdPartyString(Serialize):
    def serialize(value: &ThirdPartyString, s: &Serializer):
        ...
```

Other words considered:

- `satisfy ThirdPartyString(Serialize):`
- `impl ThirdPartyString(Serialize):`
- `attach ThirdPartyString(Serialize):`
- `augment ThirdPartyString(Serialize):`
- `enrich ThirdPartyString(Serialize):`
- `patch ThirdPartyString(Serialize):`
- `open ThirdPartyString(Serialize):`
- `add ThirdPartyString(Serialize):`
- `more ThirdPartyString(Serialize):`
- `extra ThirdPartyString(Serialize):`
- `plus ThirdPartyString(Serialize):`
- `mixin ThirdPartyString(Serialize):`
- `instance ThirdPartyString(Serialize):`
- `realize ThirdPartyString(Serialize):`
- `fulfill ThirdPartyString(Serialize):`
- `provide ThirdPartyString(Serialize):`
- `derive ThirdPartyString(Serialize):`
- `role ThirdPartyString(Serialize):`
- `view ThirdPartyString(Serialize):`

Current preference from the discussion:

- `extend ThirdPartyString(Serialize):` is the most class-shaped.
- `adapt ThirdPartyString(Serialize):` communicates third-party glue well.
- `impl Serialize for ThirdPartyString:` is the most precise, but looks Rusty.

## Coherence

External conformance needs a coherence rule. At most one visible implementation
of a given `(protocol, type)` pair should exist in one compile graph.

```python
impl Serialize for ThirdPartyString:
    ...

impl Serialize for ThirdPartyString:
    ...  # error: duplicate visible conformance
```

Without this rule, generic code can become ambiguous depending on import order.

## Protocol-Like Abstract Classes

External conformance should probably be restricted to protocol-like abstract
classes that do not require instance storage. This is fine:

```python
class Hashable:
    @abstract
    def hash(self) -> u64:
        pass
```

This is more complicated:

```python
class WidgetBase:
    layout_id: u64

    @abstract
    def draw(self):
        pass
```

`WidgetBase` has storage. Externally conforming an unrelated type to it cannot
add a base subobject without changing layout. That should either be rejected or
require an explicit wrapper/type-erasure object.

## Serde Target

Serde should be a library and macro/decorator target, not a compiler builtin.
Rust has Serde as the model people love. C++ has useful libraries in the same
space, such as cereal, Boost.Serialization, reflect-cpp, bitsery, and other
project-specific reflection/serialization systems, but there is no single
standard equivalent that feels like Rust Serde.

The language should provide enough macro and protocol machinery for a library
to write this:

```python
from serde import serde

@serde
class Player:
    hp: i32
    name: str
```

`@serde` is a library-provided shorthand for generating serialize and
deserialize support. The more general spelling can stay available:

```python
@derive(Serialize, Deserialize)
class Player:
    hp: i32
    name: str
```

The pure inheritance spelling is also Python-shaped:

```python
class Serde(Serialize, Deserialize):
    pass

class Player(Serde):
    hp: i32
    name: str
```

That form is useful when a class genuinely implements the methods itself. It
does not generate boilerplate by itself. A macro/decorator still carries the
value of inspecting fields and generating the obvious methods.

Field and class metadata should stay Python-shaped:

```python
@serde(rename_all="camelCase")
class Player:
    hp: i32

    @serde(rename="displayName")
    name: str

    @serde(skip)
    cached_texture: TextureHandle
```

The macro should inspect the class fields, types, decorators, and doc metadata,
then generate normal Dudu declarations that participate in type checking, LSP,
and codegen.

## Serde For Third-Party Types

A Serde-like library needs a way to support native or third-party types without
editing their declarations.

External conformance shape:

```python
impl Serialize for ThirdPartyString:
    def serialize(value: &ThirdPartyString, s: &Serializer):
        s.string(value.c_str())

impl Deserialize for ThirdPartyString:
    def deserialize(d: &Deserializer) -> Result[ThirdPartyString, Error]:
        return d.string().map(ThirdPartyString)
```

Decorator adapter shape:

```python
@serde_for(ThirdPartyString)
class ThirdPartyStringSerde:
    def serialize(value: &ThirdPartyString, s: &Serializer):
        s.string(value.c_str())

    def deserialize(d: &Deserializer) -> Result[ThirdPartyString, Error]:
        return d.string().map(ThirdPartyString)
```

Likely lookup order for a Serde library:

1. use generated or declared serialize/deserialize support on the type
2. use a visible external conformance or `@serde_for(Type)` adapter
3. fail with a diagnostic naming the missing type and field path

## Associated Types And Traits

The discussion touched on trait-like associated types:

```python
class Iterator:
    type Item

    @abstract
    def next(self) -> Option[Self.Item]:
        pass
```

This is powerful, but it is a larger language feature than abstract classes. It
can push Dudu toward a C++/Rust-style type-level system, with more compiler
complexity and slower diagnostics.

The design direction is:

- do not rush a separate `trait`, `interface`, or `protocol` keyword
- use abstract classes for the first protocol-shaped use cases
- reserve associated types/type functions for real pressure from libraries such
  as iterators, tensor shapes, units, serializers, or parser combinators
- keep `type` as an alias/forward-declaration concept unless a dedicated
  associated-type design is accepted

## Type-Level Data

Tensor shapes brought up a broader idea: Dudu can sometimes carry data in types
to prevent impossible states at compile time.

Examples:

```python
image: Tensor[f32][Batch, Height, Width, Channels]
kernel: Tensor[f32][KernelH, KernelW]
out: Tensor[f32][Batch, Height - KernelH + 1, Width - KernelW + 1]
```

The same idea could apply outside numeric computing:

- physical units such as `Meters`, `Seconds`, and `MetersPerSecond`
- protocol states such as `Socket[Open]` versus `Socket[Closed]`
- fixed packet layouts and buffer capacities
- parser states and grammar phases
- ordinal/number-tower experiments

This is powerful but expensive language surface. It should not be used to solve
every ordinary generic problem. The practical rule from the tensor discussion is
to keep runtime shapes and ordinary values as the default, then use static
shape/type data at API boundaries where it clearly improves errors, layout, GPU
dispatch, or impossible-state checking.

## Open Questions

- Which external conformance spelling is the right balance of Python-looking
  and precise?
- Should external conformance support runtime type-erasure adapters, or only
  generic/static dispatch?
- Should `@serde` be a normal decorator macro layered on `@derive`, or should
  `@derive(Serialize, Deserialize)` remain the only blessed shape?
- How should generated macro code appear in LSP hover, go-to-definition, and
  "show generated C++" views?
- How much structural matching should libraries be allowed to request without
  making conformance accidental globally?
