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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build the compiler with developer tests enabled:

```sh
./scripts/build.sh
```

Show the compiler-driver commands:

```sh
./build/duc --help
./build/duc --version
```

Use `dudu` for normal project work. It is the Cargo-like front door for creating
projects, building, running, testing, and cleaning:

```sh
./build/dudu init hello
cd hello
../build/dudu run
../build/dudu test
../build/dudu clean
```

Use `duc` for direct compiler-driver actions, similar to how `rustc` sits under
Cargo:

```sh
./build/duc check tests/fixtures/simple_program.dd
./build/duc fmt tests/fixtures/simple_program.dd
./build/duc fmt tests/fixtures/simple_program.dd --check
./build/duc emit tests/fixtures/simple_program.dd
./build/duc run tests/fixtures/run_zero.dd
./build/duc tests/fixtures/c_api_export.dd --emit-c-header -
./build/duc emit examples/compile_time.dd -DDEBUG=true -DRENDER_BACKEND=raylib
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

Manifest paths are resolved from the directory containing `dudu.toml`, not from
the shell's current working directory. Explicit local imports such as
`import "./math.dd"` or `import cpp "./local.hpp"` remain relative to the source
file that contains the import.

Then run project commands from the project directory:

```sh
../build/dudu check
../build/dudu build
../build/dudu run
../build/dudu test
../build/dudu clean-cache
```

`dudu build`, `dudu run`, and `dudu test` are intended to stay the normal
front door even for serious native projects. The direct compiler path is the
fast narrow backend. CMake is the broad native ecosystem backend for larger
dependency graphs, package discovery, IDE generators, and user-owned native
builds. `dudu cmake` is an inspectable escape hatch, not the replacement for
`dudu build`.

`dudu.toml` is the canonical Dudu project file, but it does not need to own
every native compilation detail. For simple projects it can describe the whole
build. For larger C/C++ projects it can select a user-owned CMake backend while
CMake remains the authority for native target definitions, toolchain files,
platform conditionals, install rules, and vendored native dependency setup.
The user still enters through `dudu build`/`run`/`test`; the backend boundary is
explicit instead of pretending TOML can replace all native build systems.
Generated CMake under the build directory is Dudu-owned and may be overwritten.
User-owned `CMakeLists.txt` files are not patched by Dudu; they should consume
stable generated Dudu artifacts explicitly.

Project manifests can also define named targets:

```toml
name = "tools"

[targets.app]
entry = "src/main.dd"
kind = "executable"

[targets.tests]
entry = "tests/main.dd"
kind = "executable"

[targets.app.pkg]
libs = ["raylib"]

[targets.tests.sources]
cpp = ["tests/support.cpp"]
```

```sh
../build/dudu run app
../build/dudu build tests
../build/dudu test
```

If an input file matches a target entry, Dudu applies that target's settings for
direct file builds too.

Generated headers are available for C and C++ integration:

```sh
./build/duc emit examples/cpp_library.dd -o build/cpp_library.cpp
./build/duc examples/cpp_library.dd --emit-header build/cpp_library.hpp
```

Run the core repository validation suite:

```sh
./scripts/test.sh
```

This suite avoids package-SDK examples that require raylib, SDL3, OpenCV,
Vulkan, FFmpeg, and similar installs.

Run optional native interop probes:

```sh
./scripts/probe_optional.sh
```

These probes compile examples against real libraries such as raylib, SDL3,
OpenCV, Vulkan, and FFmpeg when those libraries are available. They are for
Dudu compiler development, not for normal users building the compiler. Missing
packages are skipped.

Run both core validation and optional native probes:

```sh
./scripts/test_full.sh
```

For dev machines that do not have raylib or SDL3 through the system package
manager, install local probe-only copies into the ignored `third_party/`
directory:

```sh
./scripts/setup_dev_deps.sh raylib
./scripts/setup_dev_deps.sh sdl3
source scripts/dev_env.sh
./scripts/probe_optional.sh
```

Run the scalar-loop benchmark comparison:

```sh
./scripts/bench.sh
./scripts/bench.sh 10000000
./scripts/bench.sh 10000000 --emit-report build/benchmarks/report.json
./scripts/bench.sh 10000000 --samples 5 --emit-report build/benchmarks/report.json
./scripts/bench.sh 10000000 --max-ratio 1.10
```

Install this checkout locally:

```sh
./scripts/install-local.sh
```

By default this installs `dudu` and `duc` under `~/.local/bin`, docs under
`~/.local/share/doc/dudu`, and editor support under
`~/.local/share/dudu/editors`. Use
`./scripts/install-local.sh --prefix /path/to/prefix` to choose a different
install root.

Editor support lives in:

- `editors/vscode`
- `editors/vim`
- `editors/nvim`

The VS Code folder is a local extension with `.dd` highlighting and command
palette actions for formatting, checking, building, and running Dudu files.
The planned language server is tracked in
[`docs/language-server-plan.md`](docs/language-server-plan.md).
Start the current language server with:

```sh
duc lsp
```

## Working Name

`dudu` is a working name. It is short, easy to type, and intentionally a little
plain.
