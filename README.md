# dudu

Dudu is a sketch for a statically typed Python subset that compiles to readable
C++.

The goal is Python syntax people already know, C/C++-style types and data
movement, native-speed output, full access to existing C and C++ libraries, and
generated `.hpp/.cpp` files that C++ projects can use directly. Source files use
`.dd`.

```python
import cpp "raylib.h" as rl


class PlayerState:
    hp: i32
    x: f32
    y: f32


def main() -> i32:
    player = PlayerState(hp=100, x=400.0, y=300.0)

    rl.InitWindow(800, 600, "dudu")

    while not rl.WindowShouldClose():
        rl.BeginDrawing()
        rl.ClearBackground(rl.BLACK)
        rl.DrawCircle(i32(player.x), i32(player.y), 40.0, rl.RED)
        rl.EndDrawing()

    rl.CloseWindow()
    return 0
```

## Current Status

The checked-in compiler is being rebuilt around the typed Python subset
described in the appearance spec and compiler plan.

The starting point is:

- [Project goals](docs/goals.md)
- [Appearance spec](docs/appearance-spec.md)
- [Python subset compiler plan](docs/python-subset-compiler-plan.md)
- [Language sketch](docs/language.md)
- [Interop plan](docs/interop.md)
- [Compiler plan](docs/compiler-plan.md)
- [Development plan](docs/development-plan.md)

Build the compiler:

```sh
./scripts/build.sh
```

Emit C++ for current compiler fixtures:

```sh
./build/dudu --help
```

Validate the checked-in examples that do not need external libraries:

```sh
./scripts/test.sh
```

## Working Name

`dudu` is a working name. It is short, easy to type, and intentionally a little
plain.
