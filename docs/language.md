# Language Sketch

This document is a draft, not a final spec.

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
```

Use `ret`, not `return`.

```dudu
fn add i32
    a i32
    b i32

    ret a + b
```

## Structs

Struct fields are one per line:

```dudu
struct Particle
    pos Vec2
    vel Vec2
    life f32
```

This maps directly to plain C++ fields unless attributes or interop rules say
otherwise.

## Variables

Inside function bodies, declarations should be explicit:

```dudu
fn main i32

    var count i32 = 0
    let max_count = 100
    ret count
```

`var` is mutable. `let` is immutable.

Type inference is allowed where the initializer makes the type obvious.

## Calls

Function calls omit parentheses in the common case:

```dudu
draw_circle 400 300 40 red
```

Method-style calls are allowed:

```dudu
player.damage 10
```

The compiler may require parentheses later for ambiguous expressions, but the
default style should avoid them.

## Control Flow

```dudu
while not window_should_close
    update
    draw
```

```dudu
if hp <= 0
    die
else
    keep_going
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

## Naming Conventions

These are draft conventions:

- Types: `PascalCase`
- Functions and variables: `snake_case`
- Built-in scalar types: `i32`, `u32`, `f32`, `f64`, `bool`, `usize`

The parser should not depend on capitalization for correctness unless the
language explicitly chooses that later.

## Open Questions

- Should no-argument functions require a blank line before body?
- Should declarations support `name Type = value` without `var`?
- How much expression precedence should exist before parentheses become needed?
- Should method declarations live under `impl Type`, or use `fn Type.method`?
- How should templates/generics look without adding punctuation soup?
