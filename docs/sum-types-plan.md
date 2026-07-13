# Sum Types And Pattern Matching Plan

Dudu should have safe Rust-style sum types and exhaustive pattern matching.

Existing Dudu `enum` declarations are simple C-like scoped enums. That syntax
should grow into one unified enum model: variants use `PascalCase`, and variants
may optionally carry payload data. A plain integer enum is the zero-payload case
with an optional backing type. Do not use the keyword `union` for this feature:
C and C++ programmers read `union` as an untagged memory overlay, while this
feature is a safe tagged sum type.

## Goals

- Make Rust-style enums available in Python-shaped syntax.
- Support heterogeneous-but-static data without `Any`.
- Give users powerful exhaustive `match`.
- Lower to readable C++.
- Keep C ABI and imported C unions separate from Dudu-native safe sum types.

## Syntax

Simple integer-like enum:

```python
enum Direction:
    North
    South
    East
    West
```

With explicit backing type and values:

```python
enum Color: u8
    Red = 1
    Green = 2
    Blue = 3
```

If every variant has no payload, Dudu can lower to a normal C++ `enum class`
with the requested backing type. If any variant has payload data, Dudu lowers to
a tagged sum type. Both forms use the same `EnumName.VariantName` spelling.

Payload variants use class-like variant declarations:

```python
enum Message:
    Quit

    Move:
        x: i32
        y: i32

    Write:
        text: str

    ChangeColor:
        r: u8
        g: u8
        b: u8
```

Construction:

```python
msg = Message.Move(x=10, y=20)
done = Message.Quit
line = Message.Write(text="hello")
```

Payload declarations always name their fields. Dudu does not support
tuple-style declarations such as `Ident(str)`. Field names make declarations,
hover information, generated documentation, diagnostics, and API changes easier
to understand:

```python
enum Token:
    Eof

    Number:
        value: i64

    Ident:
        text: str
```

Named fields do not make construction or matching verbose. Generated variant
constructors accept positional or named arguments:

```python
token = Token.Ident("name")
explicit = Token.Ident(text="name")

match token:
    case Token.Ident(text):
        print(text)
```

The declaration defines the meaning and order of the payload. Positional calls
are concise when that order is obvious; named calls remain available when it is
not.

## Matching

Use Python-shaped `match` and `case`:

```python
def handle(msg: Message) -> i32:
    match msg:
        case Message.Quit:
            return 0
        case Message.Move(x, y):
            return x + y
        case Message.Write(text):
            print(text)
            return 1
        case Message.ChangeColor(r, g, b):
            return i32(r) + i32(g) + i32(b)
```

Named binding should also work:

```python
match msg:
    case Message.Move(x=dx, y=dy):
        player.x += f32(dx)
        player.y += f32(dy)
```

Wildcard:

```python
match msg:
    case Message.Quit:
        return
    case _:
        log.debug("ignored message")
```

Guards:

```python
match token:
    case Token.Number(value) if value > 0:
        return value
    case Token.Number(value):
        return -value
    case _:
        return 0
```

`match` also supports ordinary value cases for integer, string, enum-like, and
other comparable values:

```python
match key:
    case "fps":
        return show_fps()
    case "cpu":
        return show_cpu()
    case _:
        return show_default()
```

Value matches are ordered comparison chains, not exhaustive sum-type matches.
Use `_` when unmatched values need a default branch.

## Exhaustiveness

For sum types, `match` should be exhaustive by default:

```python
def handle(msg: Message):
    match msg:
        case Message.Quit:
            return
```

Diagnostic:

```text
error[dudu/match-exhaustive]: non-exhaustive match on Message
missing cases: Message.Move, Message.Write, Message.ChangeColor
```

Using `case _` makes the match exhaustive. The compiler should warn when a case
is unreachable because a previous pattern already covers it.

## Option And Result

`Option[T]` and `Result[T, E]` are matchable:

Conceptually, their payload variants have named fields:

```python
enum Option[T]:
    None

    Some:
        value: T

enum Result[T, E]:
    Ok:
        value: T

    Err:
        error: E
```

Construction and matching can still use concise positional calls and patterns:

```python
match maybe_player:
    case Some(player):
        update(player)
    case None:
        return
```

```python
match result:
    case Ok(value):
        return value
    case Err(err):
        log_error(err)
        return fallback
```

The exact constructor names for `Option` need to stay consistent with existing
Dudu `None` usage. Pattern matching should not make null pointers look like
`Option`.

Status: implemented. `Option[T]` supports `Some(value)`, `None`, and `_`.
`Result[T, E]` supports `Ok(value)`, `Err(error)`, and `_`. Wrapper match
cases are exhaustive, and guarded wrapper cases follow the same rule as enum
guards: they do not count toward exhaustiveness.

## Current Status

Simple zero-payload enums lower to C++ `enum class` and support exhaustive
`match` through ordered generated comparisons.

Payload enums now lower to a tagged `std::variant` wrapper with one nested
struct per variant. Variant constructors such as `Message.Move(x=10, y=20)`,
positional constructor calls such as `Message.Write("hello")`, and zero-payload
variants inside payload enums such as `Message.Quit` construct the enclosing
enum value.

Payload `match` lowers to `std::holds_alternative`/`std::get` checks and
supports positional and named bindings:

```python
match msg:
    case Message.Move(x, y):
        return x + y
```

```python
match msg:
    case Message.Move(y=dy, x=dx):
        return dx + dy
```

Match guards are implemented and type-checked as `bool`. Guarded cases do not
count toward exhaustiveness, so a guarded variant must still have an unguarded
case or a wildcard case somewhere later in the match.

Recursive enum examples are covered with pointer indirection, matching the
planned ownership-neutral shape for recursive data.

Payload enum C++ emission supports payload fields whose types are Dudu-native
classes declared in the same module. The emitter forward-declares enums,
emits classes, then emits full enum definitions so class payloads such as
`Command.Move(dir: Vec2)` compile without wrapper ordering tricks.

Unreachable diagnostics reject an unguarded `_` case after all enum or wrapper
cases are already covered, reject later cases after an unguarded wildcard has
already covered the rest of the match, and reject guarded or unguarded variant
cases that appear after an earlier unguarded case for the same variant.

Anonymous `variant[...]` is implemented as a native Dudu type form that lowers
to `std::variant<...>`. Assignment is allowed only when the value matches
exactly one alternative. This supports explicit mixed containers such as
`list[variant[i32, str]]` without adding dynamic `Any`.

Open work: richer subsumption only matters when the pattern language gains
nested alternatives or range-like patterns.

## Recursive Data

Recursive enums should require an indirection, matching C++/Rust reality:

```python
enum Expr:
    Number:
        value: f64

    Add:
        left: *Expr
        right: *Expr
    Mul:
        left: *Expr
        right: *Expr
```

Higher-level owning pointers can be used when the ownership model is designed:

```python
enum Expr:
    Number:
        value: f64

    Add:
        left: unique[Expr]
        right: unique[Expr]
```

## Real Target Examples

These examples are the suite we should make compile and run as the feature
lands. They should include positive fixtures, executable examples, and negative
diagnostics for missing cases, unreachable cases, and invalid destructuring.

### Lexer Tokens

```python
enum Token:
    Eof

    Ident:
        text: str

    IntLit:
        value: i64

    FloatLit:
        value: f64

    StringLit:
        value: str

    Punct:
        ch: u8
```

Status: an executable lexer-token fixture covers named payload declarations,
positional and named construction, guarded payload cases, named destructuring,
and exhaustive matching. A negative fixture rejects tuple-style payload
declarations with a replacement diagnostic.

### UI Events

```python
enum Event:
    Quit

    KeyDown:
        key: i32

    KeyUp:
        key: i32

    MouseMove:
        x: f32
        y: f32

    MouseButton:
        button: i32
        down: bool
```

Status: an executable event/message fixture covers UI-style events with named
payloads, guarded button cases, and exhaustive matching.

### Game Commands

```python
enum Command:
    Idle

    Move:
        dir: Vec2[f32]

    Attack:
        target: EntityId

    CastSpell:
        spell: SpellId
        target: EntityId
```

Status: the event/message fixture covers command-style enum payloads that carry
a Dudu-native class value.

### Network Messages

```python
enum NetMessage:
    Hello:
        version: u32

    Input:
        frame: u64
        buttons: u32

    Snapshot:
        frame: u64
        entities: list[EntitySnapshot]

    Disconnect:
        reason: str
```

Status: the event/message fixture covers network-style messages with integer
and string payloads.

### Parse Errors

```python
enum ParseError:
    UnexpectedToken:
        expected: str
        got: Token

    UnexpectedEof

    InvalidNumber:
        text: str
```

## Anonymous Variants

Dudu can add an anonymous `variant[A, B, C]` type for narrow interop and local
uses, but named enums should be the preferred user-facing feature.

Explicit mixed list:

```python
items: list[variant[i32, str, bool]] = []
items.append(1)
items.append("hello")
items.append(True)
```

Named sum type:

```python
enum Item:
    Number:
        value: i32

    Text:
        value: str

    Flag:
        value: bool

items: list[Item] = []
```

The named form gives better errors, better match cases, and better public API.

## Lowering

Possible C++ lowering:

```cpp
struct Message_Quit {};
struct Message_Move { int32_t x; int32_t y; };
struct Message_Write { std::string text; };
struct Message_ChangeColor { uint8_t r; uint8_t g; uint8_t b; };

using MessageStorage =
    std::variant<Message_Quit, Message_Move, Message_Write, Message_ChangeColor>;

struct Message {
    MessageStorage storage;

    static Message Quit();
    static Message Move(int32_t x, int32_t y);
    static Message Write(std::string text);
    static Message ChangeColor(uint8_t r, uint8_t g, uint8_t b);
};
```

The generated shape can change, but the public semantics should be:

- exactly one active variant
- value semantics unless the payload contains pointers/references
- no implicit conversion between different enum types
- deterministic layout only when explicitly requested by a separate layout
  feature

## AST Requirements

This feature depends on [AST Plan](ast-plan.md).

Needed nodes:

- enum variants with payload fields
- pattern AST nodes
- match statement/expression nodes
- guard expressions
- binding patterns
- wildcard patterns
- exhaustiveness checker data

## Diagnostics

Required diagnostics:

- duplicate variant name
- duplicate payload field name
- unknown variant in pattern
- wrong payload arity
- wrong named payload field
- binding name collision
- non-exhaustive match
- unreachable case
- guard expression is not bool
- recursive enum without indirection
- mixed list literal needs explicit sum type

## Acceptance

- Simple existing enums use PascalCase variants.
- Payload enums compile and run.
- `match` is exhaustive for payload enums.
- `Option` and `Result` can be matched.
- Generated C++ is readable.
- Errors point at Dudu source.
- Mixed containers require explicit `variant[...]` or a named enum.

Status: simple C-like enum variants are PascalCase in fixtures and compiler
naming checks. Snake-case enum variants are rejected, so the enum surface
matches the planned `EnumName.VariantName` spelling.

Status: payload variant syntax parses named field blocks into the AST.
Tuple-style payload declarations are rejected. Payload fields are validated for
known types, duplicate names, and snake_case names. Payload enums lower to
tagged `std::variant` wrappers and support exhaustive `match`.

Status: `match` and `case` statements parse into statement AST nodes instead of
being recognized only by a raw unsupported-prefix check. The AST records the
match subject, each case pattern, optional case guards, and parsed guard
expressions. Simple zero-payload enum matches lower to ordered C++ comparisons,
require `EnumName.VariantName` or `_` cases, reject unknown and duplicate cases,
and enforce exhaustiveness. Payload destructuring, guards, `Option`, and
`Result` matching are implemented.

Optimization note: value matches and guarded zero-payload enum matches now lower
to a lean ordered chain without a generated `matched` boolean:

```cpp
if (subject == "/") return index_route(request);
if (subject == "/health") return health_route(request);
return not_found_route(request);
```

Non-returning value matches use the same ordered shape with `else if` / `else`,
so assignment-style cases do not reevaluate a `matched` guard on every branch.

Status: anonymous `variant[...]` parses as a template type, validates its
alternative types, lowers to `std::variant`, and supports direct assignment
from exactly one matching alternative.
