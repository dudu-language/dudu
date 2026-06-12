# dudu

Dudu is a statically typed Python-shaped language that compiles to readable C++.

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

The checked-in compiler parses `.dd`, checks the core typed subset, emits
readable C++20, emits importable headers, formats source, handles multi-file
Dudu imports, and validates the canonical examples that do not require external
SDKs.

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

Show the compiler commands:

```sh
./build/duc --help
./build/duc --version
```

Check, format, or emit code:

```sh
./build/duc check tests/fixtures/simple_program.dd
./build/duc fmt tests/fixtures/simple_program.dd
./build/duc fmt tests/fixtures/simple_program.dd --check
./build/duc emit tests/fixtures/simple_program.dd
./build/duc run tests/fixtures/run_zero.dd
./build/dudu examples/cpp_library.dd --emit-header -
./build/duc emit examples/compile_time.dd -DDEBUG=true -DRENDER_BACKEND=raylib
```

Build flags can also live beside an input file in `dudu.toml`:

```toml
[build]
DEBUG = true
RENDER_BACKEND = "raylib"
```

Project commands can use a top-level `main` entry:

```toml
main = "src/main.dd"
```

Then run:

```sh
./build/duc check
./build/duc emit -o build/main.cpp
./build/duc run
```

Project tests can be wired through `dudu.toml`:

```toml
[bench]
command = "./scripts/bench.sh"

[test]
command = "./scripts/test.sh"
```

```sh
./build/duc bench 10000000
./build/duc test
```

Validate the checked-in examples that do not need external libraries:

```sh
./scripts/test.sh
```

Run the scalar-loop benchmark comparison:

```sh
./scripts/bench.sh
./scripts/bench.sh 10000000
```

Editor syntax files live in:

- `editors/vscode`
- `editors/vim`

## Working Name

`dudu` is a working name. It is short, easy to type, and intentionally a little
plain.
