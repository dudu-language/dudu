# Developer Guide

This page keeps the developer-heavy command notes out of the main README.

For what belongs in each top-level directory, see
[Repository Layout](repo-layout.md).

## Build From Source

Build the compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build with developer tests enabled:

```sh
./scripts/build.sh
```

Show compiler-driver commands:

```sh
./build/duc --help
./build/duc --version
```

## Project Driver

Use `dudu` for normal project work. It is the Cargo-like front door for
creating projects, building, running, testing, and cleaning:

```sh
./build/dudu init hello
cd hello
../build/dudu run
../build/dudu test
../build/dudu clean
```

Run project commands from a directory containing `dudu.toml`:

```sh
dudu check
dudu build
dudu run
dudu test
dudu fmt
dudu clean
dudu clean-cache
```

If an input file matches a target entry, Dudu applies that target's settings for
normal CMake-backed `dudu build` and `dudu run` commands too.

## Compiler Driver

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

Generated headers are available for C and C++ integration:

```sh
./build/duc emit examples/cpp_library.dd -o build/cpp_library.cpp
./build/duc examples/cpp_library.dd --emit-header build/cpp_library.hpp
```

## Manifest Paths

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
dudu run app
dudu build tests
dudu test
```

## Build Diagnostics

Add `--timings` to project commands when a build feels slow:

```sh
dudu run --timings
```

This prefixes Dudu progress lines with elapsed time so long analysis, emit,
CMake, compile, or run phases are visible without changing program stdout.

## Build Backends

`dudu build`, `dudu run`, and `dudu test` are intended to stay the normal front
door even for serious native projects. `dudu build` and `dudu run` use generated
or user-owned CMake, including for small single-file projects, so project builds
exercise the same native ecosystem path as larger dependency graphs, package
discovery, IDE generators, and user-owned native builds. `dudu cmake` is an
inspectable escape hatch, not the replacement for `dudu build`.

`dudu.toml` is the canonical Dudu project file, but it does not need to own
every native compilation detail. For simple projects it can describe the whole
build. For larger C/C++ projects it can declare a user-owned CMake source tree
and target while CMake remains the authority for native target definitions,
toolchain files, platform conditionals, install rules, and vendored native
dependency setup.

Generated CMake under the build directory is Dudu-owned and may be overwritten.
User-owned `CMakeLists.txt` files are not patched by Dudu; they should consume
stable generated Dudu artifacts explicitly. The scaffolded CMake file from
`dudu init`/`dudu new` runs `duc emit-modules`, discovers generated module
`.cpp` files, and then belongs to the project.

## Validation

Run the fast validation suite:

```sh
./scripts/test_fast.sh
```

Run the broader repository validation suite:

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

## Benchmarks

Run the scalar-loop benchmark comparison:

```sh
./scripts/bench.sh
./scripts/bench.sh 10000000
./scripts/bench.sh 10000000 --emit-report build/benchmarks/report.json
./scripts/bench.sh 10000000 --samples 5 --emit-report build/benchmarks/report.json
./scripts/bench.sh 10000000 --max-ratio 1.10
```

## Editor Support

Editor support lives in:

- `editors/vscode`
- `editors/vim`
- `editors/nvim`

The VS Code folder is a local extension with `.dd` highlighting and command
palette actions for formatting, checking, building, and running Dudu files.

The planned language server is tracked in
[`language-server-plan.md`](language-server-plan.md). Start the current
language server with:

```sh
duc lsp
```
