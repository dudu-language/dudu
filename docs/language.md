# Language Sketch

This document is a draft, not a final spec.

## Source Files

Dudu source files use `.dd`.

## Core Shape

Blocks are indentation-based. Function parameters live on the following lines.
A blank line separates the function signature from the body.

```dudu
fn name ReturnType
    arg Type
    arg Type

    body
```

The return type is optional. Omitted return type means `void`.

```dudu
fn tick
    world ref mut World
    dt f32

    world.time += dt
    simulate world dt
```

## Return Values

Blocks evaluate to their last expression. A function with a return type returns
the value of its body block.

```dudu
fn add i32
    a i32
    b i32

    a + b
```

`ret` is only for early return.

```dudu
fn clamp01 f32
    x f32

    if x < 0
        ret 0
    if x > 1
        ret 1

    x
```

## Records

Use `rec` for plain aggregate types.

```dudu
rec Particle
    pos Vec2
    vel Vec2
    life f32
```

This maps to a C++ `struct` unless attributes or interop rules say otherwise.
Dudu does not need a separate class form for v1.

Record construction uses the type name as a constructor-like expression.

```dudu
pos Vec2 = Vec2 10 20
```

This should emit aggregate-style C++:

```cpp
Vec2 pos = Vec2{10, 20};
```

Named-field construction can wait until later.

## Enums

Enums can optionally name their underlying integer type.

```dudu
enum Direction
    up
    down
    left
    right
```

```dudu
enum Team u8
    red = 0
    blue = 1
```

Enums should map to scoped C++ enums by default:

```cpp
enum class Team : uint8_t {
    red = 0,
    blue = 1,
};
```

## Type Aliases

Use `type` for typedef-style aliases.

```dudu
type EntityId u32
type ByteSpan span u8
type Position Vec2
```

This should emit `using` aliases in C++.

## Types

Dudu is statically typed. Types are known at compile time and should lower
directly to C++ types.

Built-in scalar types:

```dudu
bool
i8
i16
i32
i64
u8
u16
u32
u64
usize
f32
f64
void
```

Text types are still an open design point. The first compiler can start with C
strings for interop:

```dudu
cstr
```

The common type forms are:

```dudu
T
ref T
ref mut T
ptr T
ptr const T
span T
arr T 16
```

Rough C++ lowering:

```dudu
i32            # int32_t
f32            # float
T              # T
ref T          # const T&
ref mut T      # T&
ptr T          # T*
ptr const T    # const T*
span T         # std::span<T>
arr T 16       # std::array<T, 16>
```

Imported C/C++ types keep their namespace alias:

```dudu
pos rl.Vector2
values std.vector i32
```

Generic/template spelling is not settled. `std.vector i32` is a placeholder for
future C++ template interop.

## Locals

Local declarations do not need `var` or `let`.

```dudu
fn main i32

    count i32 = 0
    name str = "dudu"
    player Player

    count
```

Locals are mutable by default, like C/C++.

Use `con` when mutation should be rejected:

```dudu
con max_count i32 = 100
con pi f32 = 3.141592
```

Type inference is allowed where the initializer makes the type obvious:

```dudu
count = 10
scale = 2.0
```

If the name already exists in the current scope, `name = value` is assignment.
If it does not exist, `name = value` is an inferred local declaration. This is
convenient, but the compiler should produce a clear diagnostic when the rule
would hide a typo.

## Calls

Function calls omit parentheses in the common case:

```dudu
draw_circle 400 300 40 red
```

Method-style calls are allowed:

```dudu
player.damage 10
```

Method declarations can use dotted function names:

```dudu
fn Player.damage
    self ref mut Player
    amount i32

    self.hp -= amount
```

The compiler may require parentheses later for ambiguous expressions, but the
default style should avoid them.

## Control Flow

`if` is an expression when all branches produce compatible values.

```dudu
fn max i32
    a i32
    b i32

    if a > b
        a
    else
        b
```

It can also be used only for effects:

```dudu
if hp <= 0
    die
else
    keep_going
```

Loops:

```dudu
while not window_should_close
    update
    draw
```

```dudu
for item in items
    draw item
```

```dudu
for i in 0..count
    print i
```

`while` and `for` evaluate to `void`.

Loop control:

```dudu
break
continue
```

## Reference And Pointer Spelling

Avoid `&` and `*` in source when possible. Use words:

```dudu
ref T
ref mut T
ptr T
ptr const T
span T
arr T 16
```

These are C/C++-style references, pointers, spans, and arrays. They are not
Rust ownership rules.

Example:

```dudu
fn move_particle
    p ref mut Particle
    dt f32

    p.pos.x += p.vel.x * dt
    p.pos.y += p.vel.y * dt
```

Pointer operations use words instead of C glyphs:

```dudu
p ptr Particle = addr particle
q ptr Particle = null
value Particle = at p
at p = particle
```

Working meanings:

- `addr x`: address of `x`.
- `at p`: dereference pointer `p`.
- `null`: null pointer literal.

Pointer field access can use normal dot syntax as sugar:

```dudu
p.hp
```

That can lower to `p->hp` when `p` is a pointer.

## Arrays, Spans, And Indexing

Fixed-size arrays:

```dudu
values arr i32 16
```

Indexing:

```dudu
values[0] = 10
x i32 = values[0]
```

Spans are pointer-plus-length views:

```dudu
fn sum i32
    values span i32

    total i32 = 0
    for value in values
        total += value
    total
```

C pointers and arrays should interop through `ptr T`, `ptr const T`, and `span T`.
The first compiler does not need a full slice system. It only needs:

- fixed arrays: `arr T N`
- indexing: `x[i]`
- raw pointers: `ptr T`
- pointer address/deref: `addr`, `at`
- spans as a standard view type for loops and APIs

## Includes

Dudu source imports use `include`:

```dudu
include "math.dd"
```

C and C++ header imports are separate because they have different interop rules:

```dudu
c include "stdio.h" as c
cpp include "raylib.h" as rl
```

Dot paths are used for imported names:

```dudu
c.printf "hello\n"
rl.InitWindow 800 600 "dudu"
```

## Comments

Use `#` for line comments:

```dudu
# This is a comment.
```

## Naming Conventions

These are draft conventions:

- Types: `PascalCase`
- Functions and variables: `snake_case`
- Built-in scalar types: `i32`, `u32`, `f32`, `f64`, `bool`, `usize`

The parser should not depend on capitalization for correctness unless the
language explicitly chooses that later.

## Open Questions

- Should no-argument functions require a blank line before body?
- Is inferred declaration by first assignment too typo-prone?
- How much expression precedence should exist before parentheses become needed?
- Should methods emit as free functions first or C++ member functions?
- How should templates/generics look without adding punctuation soup?
