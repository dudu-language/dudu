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

Show the compiler-driver commands:

```sh
./build/duc --help
./build/duc --version
```

Use `duc` for direct compiler-driver actions:

```sh
./build/duc check tests/fixtures/simple_program.dd
./build/duc fmt tests/fixtures/simple_program.dd
./build/duc fmt tests/fixtures/simple_program.dd --check
./build/duc emit tests/fixtures/simple_program.dd
./build/duc run tests/fixtures/run_zero.dd
./build/duc tests/fixtures/c_api_export.dd --emit-c-header -
./build/duc emit examples/compile_time.dd -DDEBUG=true -DRENDER_BACKEND=raylib
```

Use `dudu` for project-level work:

```sh
./build/dudu init hello
cd hello
../build/dudu run
../build/dudu test
../build/dudu clean
```

A `dudu.toml` project can name its entry file and native build settings:

```toml
name = "hello"
entry = "src/main.dd"

[cxx]
standard = "c++20"

[build]
dir = "build"

[include]
paths = ["include"]

[pkg]
libs = ["raylib"]
```

Then run project commands from the project directory:

```sh
../build/dudu check
../build/dudu build
../build/dudu run
../build/dudu test
../build/dudu clean-cache
```

Project manifests can also define named targets:

```toml
name = "tools"

[targets.app]
entry = "src/main.dd"
kind = "executable"

[targets.tests]
entry = "tests/main.dd"
kind = "executable"
```

```sh
../build/dudu run app
../build/dudu build tests
../build/dudu test
```

A checked-in scratch project lives in `duduplayground/`:

```sh
cd duduplayground
../build/dudu run
../build/dudu test
```

Generated headers are available for C and C++ integration:

```sh
./build/duc emit examples/cpp_library.dd -o build/cpp_library.cpp
./build/duc examples/cpp_library.dd --emit-header build/cpp_library.hpp
```

Validate the checked-in examples that do not need external libraries:

```sh
./scripts/test.sh
```

Run the scalar-loop benchmark comparison:

```sh
./scripts/bench.sh
./scripts/bench.sh 10000000
./scripts/bench.sh 10000000 --emit-report build/benchmarks/report.json
./scripts/bench.sh 10000000 --samples 5 --emit-report build/benchmarks/report.json
./scripts/bench.sh 10000000 --max-ratio 1.10
```

Editor support lives in:

- `editors/vscode`
- `editors/vim`
- `editors/nvim`

The VS Code folder is a local extension with `.dd` highlighting and command
palette actions for formatting, checking, building, and running Dudu files.

## Working Name

`dudu` is a working name. It is short, easy to type, and intentionally a little
plain.
