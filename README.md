# dudu

Dudu is a sketch for a low-syntax systems language that feels light to write
but stays close to C and C++ at runtime.

The goal is not Python semantics made fast. The goal is a Python-readable,
indentation-based language with static types, predictable C/C++-style data
movement, native-speed output, and first-class access to existing C and C++
libraries. Source files use `.dd`.

```dudu
use cpp "raylib.h" as rl

th Vec2
    x f32
    y f32

fn main i32

    rl.InitWindow 800 600 "dudu"

    while not rl.WindowShouldClose
        rl.BeginDrawing
        rl.ClearBackground rl.BLACK
        rl.DrawCircle 400 300 40 rl.RED
        rl.EndDrawing

    rl.CloseWindow
    0
```

## Current Status

This repo has a first compiler slice. It parses a small `.dd` subset and emits
readable C++.

The starting point is:

- [Project goals](docs/goals.md)
- [Language sketch](docs/language.md)
- [Interop plan](docs/interop.md)
- [Compiler plan](docs/compiler-plan.md)

Build the compiler:

```sh
./scripts/build.sh
```

Emit C++:

```sh
./build/dudu examples/use_math.dd --emit-cpp -
```

Validate the checked-in examples that do not need external libraries:

```sh
./scripts/test.sh
```

## Working Name

`dudu` is a working name. It is short, easy to type, and intentionally a little
plain.
