# glaive

Glaive is a sketch for a low-syntax systems language that feels light to write
but stays close to C and C++ at runtime.

The goal is not Python semantics made fast. The goal is a Python-readable,
indentation-based language with static types, predictable C/C++-style data
movement, native-speed output, and first-class access to existing C and C++
libraries.

```glaive
cpp include "raylib.h" as rl

struct Vec2
    x f32
    y f32

fn main i32

    rl.InitWindow 800 600 "glaive"

    while not rl.WindowShouldClose
        rl.BeginDrawing
        rl.ClearBackground rl.BLACK
        rl.DrawCircle 400 300 40 rl.RED
        rl.EndDrawing

    rl.CloseWindow
    ret 0
```

## Current Status

This repo is currently spec-first. There is no compiler yet.

The starting point is:

- [Project goals](docs/goals.md)
- [Language sketch](docs/language.md)
- [Interop plan](docs/interop.md)
- [Compiler plan](docs/compiler-plan.md)

## Working Name

`glaive` is a working name. It is short, systems-flavored, and fits the local
`g*` project naming style. The name can still change before implementation
starts.
