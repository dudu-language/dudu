# dudu

Dudu is a statically typed Python-shaped systems language that compiles to
readable C++20.

The target is simple: write code that looks close to Python, keep C/C++ style
control over types and memory, use existing C and C++ libraries directly, and
produce native code that C++ projects can consume.

Source files use `.dd`.

```python
import cpp "raylib.h" as rl


class Player:
    hp: i32
    x: f32
    y: f32


def main() -> i32:
    player = Player(hp=100, x=400.0, y=300.0)

    rl.InitWindow(800, 600, "dudu")

    while not rl.WindowShouldClose():
        rl.BeginDrawing()
        rl.ClearBackground(rl.BLACK)
        rl.DrawCircle(i32(player.x), i32(player.y), 40.0, rl.RED)
        rl.EndDrawing()

    rl.CloseWindow()
    return 0
```

## Status

Dudu is usable as an early compiler. It can parse and check the core typed
subset, format source, compile multi-file projects, import native C/C++
headers, emit readable C++20, and drive CMake-backed builds through `dudu`.

It is not a stable language release yet. The current work is focused on making
the compiler architecture, module system, native interop, diagnostics, and
tooling solid enough for real projects.

Current language coverage includes Dudu-native generics, payload enums with
exhaustive `match`, fixed arrays with matrix/tensor-style indexing and slices,
operator overloads, native inheritance, generated CMake module builds, and
initial LSP support.

## Install

Install prerequisites.

Ubuntu/Debian:

```sh
sudo apt install git cmake clang g++ build-essential
```

macOS:

```sh
xcode-select --install
brew install cmake
```

Clone, build, and install:

```sh
git clone https://github.com/wegfawefgawefg/dudu.git
cd dudu
./scripts/install-local.sh
export PATH="$HOME/.local/bin:$PATH"
dudu --version
```

`install-local.sh` installs:

- `dudu`, the project driver
- `duc`, the lower-level compiler driver
- docs
- editor support files

The default install prefix is `~/.local`. Use another prefix with:

```sh
./scripts/install-local.sh --prefix /path/to/prefix
```

## First Project

```sh
dudu init hello
cd hello
dudu run
```

Common project commands:

```sh
dudu check
dudu build
dudu run
dudu test
dudu fmt
dudu clean
```

Add `--timings` when a build feels slow:

```sh
dudu run --timings
```

## Project Files

Simple projects use `dudu.toml`:

```toml
name = "hello"
entry = "src/main.dd"

[cxx]
standard = "c++20"

[pkg]
libs = ["raylib"]
```

For larger native projects, `dudu build`, `dudu run`, and `dudu test` stay the
front door, but CMake is the serious native backend. Dudu can generate and drive
internal CMake projects, and it can also build user-owned CMake projects when
the native build needs toolchain files, platform rules, vendored C/C++ code, or
larger dependency graphs.

Generated CMake under `build/` belongs to Dudu and may be overwritten.
User-owned `CMakeLists.txt` files are never patched by Dudu. New projects get
a starter `CMakeLists.txt` that builds generated Dudu module artifacts through
`duc`; after creation, that file belongs to the project.

## Native Interop

Import C and C++ headers directly:

```python
import cpp "raylib.h" as rl
import cpp "vector" as std
```

Generated headers are available for C and C++ integration:

```sh
duc emit examples/cpp_library.dd -o build/cpp_library.cpp
duc examples/cpp_library.dd --emit-header build/cpp_library.hpp
```

Optional examples that use native libraries such as raylib, SDL3, OpenCV,
Vulkan, or FFmpeg need those libraries installed separately.

External dogfood projects can be checked locally with:

```sh
./scripts/test_examples.sh
./scripts/test_dogfood.sh
```

The example script skips missing optional package SDKs. The dogfood script
skips missing local repos and currently covers `raymarch-dd` and
`dudu-webserver` when they exist next to the Dudu checkout.

## Editor Support

Editor files live in:

- `editors/vscode`
- `editors/vim`
- `editors/nvim`

The VS Code folder is a local extension with `.dd` highlighting and command
palette actions for formatting, checking, building, and running Dudu files.

The language server currently starts with:

```sh
duc lsp
```

## Roadmap

- [x] Parse and compile a typed Python-shaped subset.
- [x] Emit readable C++20.
- [x] Drive project builds with `dudu`.
- [x] Compile multi-file Dudu projects.
- [x] Import C/C++ headers through clang-based native scanning.
- [x] Use generated CMake as the normal project backend.
- [x] Install from a checkout with `scripts/install-local.sh`.
- [x] Add native generics, payload enums, fixed arrays, slicing, operator
      overloads, and inheritance.
- [x] Emit separate generated files through the generated-CMake backend.
- [x] Complete the core AST/destringing migration for normal Dudu statements,
      expressions, types, sema, codegen, and lint paths.
- [x] Keep module imports canonical so the same `.dd` file reached through
      multiple import routes is one module, not duplicate declarations.
- [ ] Add module-level compiler invalidation for faster Dudu-side rebuilds.
- [ ] Harden native interop against common C++ libraries.
- [ ] Finish LSP hover, go-to-definition, references, diagnostics, and formatter
      support on top of the real AST.
- [ ] Add a broad compatibility suite for real libraries and larger examples.
- [ ] Add release binaries and package-manager distribution.

The full implementation plan lives in [`docs/le_plan.md`](docs/le_plan.md).
Developer command details live in [`docs/developer-guide.md`](docs/developer-guide.md).
