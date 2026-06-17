# Dudu Project Driver Plan

This plan defines the `dudu` project command.

`duc` remains the compiler driver. It should stay explicit, scriptable, and
usable without a project manifest:

```text
duc check src/main.dd
duc emit src/main.dd -o build/main.cpp
duc build src/main.dd -o build/app
```

`dudu` is the Python-friendly project driver. It reads `dudu.toml`, calls
`duc`, and coordinates normal C/C++ build tools without replacing them.
`dudu build`, `dudu run`, and `dudu test` are the stable user-facing commands;
whether they drive the direct compiler path, generated CMake, or user-owned
CMake is a backend choice.

## Goal

Make Dudu projects feel close to Python for day-to-day use:

```text
dudu init
dudu run
dudu test
dudu fmt
```

`dudu fmt` should be the normal project formatter. `duc fmt` remains the direct
file-oriented formatter underneath it.

while keeping the C/C++ build surface visible and usable:

```text
duc emit src/main.dd -o build/main.cpp
c++ build/main.cpp src/simd_support.cpp -Iinclude -lraylib -o build/app
```

The driver should smooth over include paths, link flags, generated C++ paths,
and common compile/run commands. It should not become a package manager, a
CMake replacement, or a walled garden.

## Non-Goals

- Do not hide CMake, Make, Ninja, pkg-config, or native link flags behind an
  opaque build system.
- Do not replace system package managers.
- Do not make Dudu dependencies source-only.
- Do not make `dudu.toml` required for `duc emit`, `duc check`, or explicit
  compiler-driver workflows.
- Do not promise portable binary dependency management.

## Command Split

```text
duc   compiler driver
dudu  project driver
```

`duc` should be the tool for low-level compiler actions:

```text
duc check <input.dd>
duc emit <input.dd> -o <output.cpp>
duc build <input.dd> -o <binary>
duc run <input.dd>
duc cmake <input.dd> -o CMakeLists.txt
```

`dudu` should be the tool for project actions:

```text
dudu init
dudu new <name>
dudu run [target]
dudu build [target]
dudu check [target]
dudu fmt
dudu test
dudu cmake
dudu clean
```

## Python-Friendly Behavior

`dudu init` initializes the current directory:

```text
dudu.toml
src/main.dd
README.md
```

`dudu init tools/pack` initializes a specific directory, creating it if needed.

`dudu new hello` creates a new directory:

```text
hello/
    dudu.toml
    src/main.dd
    README.md
```

Like Cargo, `dudu init` and `dudu new` initialize git and write `.gitignore`
when the project is not already inside an existing git repository. Inside an
existing repository, they skip git setup and do not add a nested `.gitignore`.

`dudu run` should be the common path. It should emit C++, build the configured
target, and launch the binary.

The command should print the major steps so users can see the native build:

```text
emit  build/dudu/main.cpp
c++   build/dudu/main.cpp src/support.cpp -Iinclude -lraylib -o build/bin/app
run   build/bin/app
```

This keeps the workflow friendly without making it magical.
Project-driver step logs go to stderr so the compiled program's stdout remains
usable for scripts and tests.

## Manifest Shape

The first `dudu.toml` should stay small:

```toml
name = "bunnymark"
entry = "src/main.dd"

[cxx]
standard = "c++20"
compiler = "c++"

[include]
paths = ["src", "include", "third_party/install/include"]

[sources]
cpp = ["src/simd_support.cpp"]
c = []

[pkg]
libs = ["raylib"]

[link]
paths = ["third_party/install/lib"]
libs = ["m", "pthread"]
flags = []

[build]
dir = "build"
```

Target decisions:

- Single-target projects use top-level `entry`.
- Multi-target projects use `[targets.<name>]`.
- Test functions use explicit `@test`; test executables may also be configured
  as native targets.
- The native build compiler defaults to `$CXX` when set, then `c++`.
- Generated CMake projects use CMake's selected C++ compiler.

Path resolution:

- `dudu.toml` paths are relative to the directory containing that manifest.
- Explicit local Dudu and native imports such as `import "./math.dd"` and
  `import cpp "./local.hpp"` are relative to the source file that wrote them.
- Native/system imports such as `import cpp "raylib.h"` search configured
  include paths, pkg-config include paths, and toolchain include paths.
- Running `dudu` from another working directory must not change behavior when
  it finds the same manifest.

## Targets

Support one target first:

```toml
name = "hello"
entry = "src/main.dd"
```

Then expand to named targets:

```toml
name = "game"

[targets.app]
entry = "src/main.dd"
kind = "executable"

[targets.tests]
entry = "tests/main.dd"
kind = "executable"
```

Targets can carry native build settings without leaking them into every other
target:

```toml
[targets.app.pkg]
libs = ["raylib"]

[targets.tools.pkg]
libs = ["sdl3"]

[targets.tools.sources]
cpp = ["third_party/imgui/imgui.cpp"]
```

When a command receives a file path that matches a target entry, the driver uses
that target's settings.

Expected commands:

```text
dudu run
dudu run app
dudu test
dudu build tests
```

Named targets are supported for `dudu build`, `dudu run`, `dudu test`, and
`dudu cmake`. `dudu test` uses `[targets.tests]` by default when that target is
present.

## Script Entries

Projects may contain more than one file with a `main` function.

The selected entry is the only executable entry point for a build or run:

```text
dudu run
dudu run src/tools/pack_assets.dd
duc run src/tools/pack_assets.dd
```

Rules:

- `dudu run` uses the configured project entry.
- `dudu run path/to/file.dd` treats that file as a script-like executable.
- `duc run path/to/file.dd` keeps the same low-level explicit behavior.
- Imported modules may contain helper functions, including functions named
  `main`; only the selected entry file's `main` should lower to C++ `main`.
- If the selected entry has no `main`, report an error. Top-level script
  statements are not part of Dudu.

## Tests

Use explicit `@test` functions rather than name-based `test_*` discovery.

```python
@test
def add_works():
    assert add(2, 3) == 5
```

Rules:

- `@test` functions take no args.
- `@test` functions may return `void`, `bool`, or `i32`.
- `void` passes unless an assertion fails.
- `bool` passes when it returns `true`.
- `i32` passes when it returns zero.
- `dudu test` discovers `@test` functions and builds a test harness.
- `dudu test src/math.dd` runs tests from one module.
- `dudu test add_works` runs one named test.
- `dudu test --filter add` filters by substring.
- `dudu test ./...` and `dudu test tests/` discover `.dd` files with `@test`
  recursively.
- Generated test binaries use `build/dudu-tests/<entry-stem>-<hash>` by
  default, so filtered and parallel runs do not stomp the same path.
- A module may contain both `main` and `@test` functions. `main` is ignored in
  test harness mode unless the module is built as an executable target.

`dudu test` now builds and runs a generated native test harness for `@test`
functions. If no test entry is configured, it can still delegate to a
configured native test command or `scripts/test.sh`.

## Native Build Inputs

The driver should pass through normal native build pieces:

- Dudu entry files.
- generated C++ files.
- hand-written C++ files.
- hand-written C files.
- include directories.
- library directories.
- linked library names.
- raw compile flags.
- raw link flags.
- pkg-config packages.

pkg-config should be a convenience input, not a separate dependency universe:

```toml
[pkg]
libs = ["raylib", "sdl3"]
```

The driver can translate that to `pkg-config --cflags --libs ...`.

## Build Backends

`dudu build`, `dudu run`, and `dudu test` are the stable front door for Dudu
projects. Backend choice is an implementation detail, not a split between
toy and real workflows.

This is the serious model for native projects. Users should not have to leave
`dudu build` just because a project grows past the direct compiler path. The
driver may choose a direct compiler backend, a generated CMake backend, or a
user-owned CMake backend, but the user-facing command remains the same.

This is not Dudu trying to replace CMake or clone another language's build
tool. Dudu's native interop promise means it must cooperate with existing C and
C++ build systems. The stable contract is the Dudu command surface; the backend
is how that command gets honest native build work done.

The build-driver contract is:

- The direct backend emits C++ and invokes the configured native compiler. It
  is real, fast, and intentionally narrow. It should handle the manifest-native
  surface it claims to support: include paths, library paths, libraries,
  compile flags, link flags, pkg-config packages, and extra C/C++ sources.
- The CMake backend is the broad native-ecosystem backend. It should support
  projects that need CMake package discovery, generated build files, IDE
  integration, platform generators, and larger native dependency graphs.
- `dudu cmake` emits an inspectable CMake artifact. It is useful for debugging,
  bootstrapping, and native handoff, but it is not a replacement for
  `dudu build`, `dudu run`, or `dudu test`.
- User-owned CMake projects are a backend mode. In that
  mode, `dudu build` configures/builds the declared CMake target and `dudu run`
  launches the configured executable.

If a backend cannot honestly model a project, it must fail clearly and point at
the backend or manifest feature that can. It should not silently drop native
inputs or guess through C/C++ build-system details.

Current implementation reality:

- `dudu build`, `dudu run`, and `dudu test` use the direct compiler backend
  by default.
- The direct backend currently emits one generated C++ translation unit for the
  Dudu source tree. Imported `.dd` files are parsed as modules, but their
  generated C++ is still merged into that one `.cpp` file before native
  compilation.
- `duc emit-modules` writes generated `.hpp/.cpp` artifacts per Dudu module
  plus a shared `dudu_runtime.hpp`.
- The generated CMake backend uses `duc emit-modules` and compiles the
  generated per-module `.cpp` files.
- The direct backend still compiles the compatibility merged `.cpp` output.
- `[build] backend = "direct"` and `[build] backend = "cmake"` parse from
  `dudu.toml`. The direct backend is selectable explicitly. The generated
  CMake backend is implemented for `dudu build`, `dudu run`, and `dudu test`;
  it emits an internal CMake project and drives `cmake -S/-B` plus
  `cmake --build`.
- The direct backend supports useful native inputs: include paths, library
  paths, libraries, compile flags, link flags, pkg-config packages, and extra
  C/C++ sources.
- `dudu cmake` emits CMake for inspection or handoff.
- User-owned CMake project integration is not implemented yet.

The next build-driver work should close that gap without changing the front
door: users should still type `dudu build`, `dudu run`, and `dudu test`.

This is not modeled after a language build tool that owns the whole world.
Dudu sits directly on the C/C++ ecosystem, so CMake support is not a fallback
for "serious" apps and it is not an admission that `dudu build` failed. It is
one of the backends that lets the same front-door command drive projects with
native package discovery, platform generators, vendored C/C++ dependencies,
and user-owned CMake files.

This is also not a Zig-style claim that Dudu's build system should replace the
project's native build system. Zig's normal model is a `build.zig` graph that
owns the build and can compile/link C and C++ inputs through that graph. Zig
build scripts can run external tools when users write that integration, but
Zig does not have a built-in "revert to CMake" backend. Dudu's interop goal is
broader in a different direction: existing CMake projects, generated CMake
projects, direct compiler builds, and plain native flags should all be valid
ways for the same Dudu command surface to reach the C/C++ ecosystem.

Put differently: Dudu is not copying Zig here. There is no plan where Dudu
tries to make every project express its whole native build as Dudu code and
then grudgingly "falls back" to CMake. CMake is one of the honest native
backends because a huge amount of C and C++ code already speaks CMake.

Dudu should not make CMake use feel like leaving the language tool. If a
project already has a serious CMake build, `dudu build` should be able to drive
that declared target. If a project does not, `dudu build` can either use the
direct backend or generate and drive CMake internally. In all cases, the user
command remains Dudu's command.

The practical target is:

- Simple native projects can use the direct backend with no generated build
  system in the way.
- Larger native projects can use the CMake backend without changing from
  `dudu build` to a different workflow.
- Existing CMake projects can remain user-owned; Dudu should drive the declared
  CMake target instead of pretending to understand every project-specific build
  rule itself.
- `dudu cmake` remains useful because users can inspect, edit, or hand off the
  generated CMake artifact when that is the right native workflow. It does not
  change the normal Dudu command surface.

Backend selection rules should be boring:

- If the manifest explicitly selects a backend, use it.
- If no backend is selected, use the simplest backend that honestly supports
  all requested native inputs.
- If the selected backend cannot model the project, fail with a diagnostic that
  names the missing capability and the backend that can support it.
- Do not silently ignore include paths, link inputs, generated files, native
  sources, package discovery, or user-owned build-system settings.

The backend is allowed to be visible in logs and artifacts. It is not allowed to
change the user's main workflow from `dudu build`, `dudu run`, and `dudu test`.

Implementation checklist:

- direct backend: emit C++ and invoke the compiler directly
- generated CMake backend: emit an internal CMake project and drive it from
  `dudu build`, `dudu run`, and `dudu test`
- user-owned CMake backend: configure/build/run declared targets from an
  existing CMake project
- diagnostics: reject unsupported backend/manifest combinations loudly
- logs: show which backend ran and where generated artifacts landed

Planned manifest shape:

```toml
[build]
backend = "direct" # or "cmake"
dir = "build"

[cmake]
source = "."
target = "game"
config = "Debug"
generator = "Ninja"
```

## CMake Emission

`dudu cmake` should emit a boring CMake project from `dudu.toml`.

The emitted file should be readable and editable. It should not depend on a
hidden Dudu build runtime.

Expected shape:

```cmake
cmake_minimum_required(VERSION 3.20)
project(bunnymark LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bunnymark
    build/dudu/main.cpp
    src/simd_support.cpp
)

target_include_directories(bunnymark PRIVATE
    src
    include
)
```

When `dudu build` uses CMake internally, it should say so in the printed steps
and leave the generated/configured files inspectable.

## Changelog

The Dudu compiler repository keeps a simple Keep a Changelog-style file:

```markdown
# Changelog

## [Unreleased]

### Added

### Changed

### Fixed
```

Project rule:

```text
When changing user-visible language behavior, CLI behavior, examples, or docs,
update CHANGELOG.md under [Unreleased].
```

This prevents important changes from being spread across transient planning
docs. Generated Dudu projects do not get a changelog by default.

## Implementation Slices

1. Add `CHANGELOG.md` to this repository.
2. Add a `dudu` executable entry point or command alias next to `duc`.
3. Implement `dudu init`.
4. Implement manifest parsing for the minimal single-target `dudu.toml`.
5. Implement `dudu check`, `dudu build`, and `dudu run`.
6. Add direct compiler invocation for simple projects.
7. Add pkg-config include/link flag expansion.
8. Add C and C++ source file passthrough.
9. Implement `dudu cmake`.
10. Add named targets.
11. Add `dudu test`.
12. Add `dudu new <name>`.
13. Update examples to prefer `dudu run` where it improves usability.
14. Keep `duc` workflows documented as the transparent fallback.
15. Add `dudu clean`.
16. Add explicit build backend selection in `dudu.toml`.
17. Add a generated-CMake backend behind `dudu build`, `dudu run`, and
    `dudu test`.
18. Support user-owned CMake projects as another backend mode behind the same
    commands.
19. Keep `dudu cmake` as artifact emission for inspection and handoff, not as
    the required workflow for serious native projects.

## Acceptance Tests

- `dudu init` creates a runnable hello project.
- `dudu run` builds and runs hello from a clean directory.
- `dudu run` prints the emitted C++ path, compiler command, and binary path.
- `dudu build` can compile a project with a hand-written `.cpp` file.
- `dudu build` can compile a project using a pkg-config package such as raylib.
- `dudu cmake` emits a readable CMake project.
- `dudu build` can use the direct backend explicitly.
- `dudu build` can use the CMake backend explicitly.
- `dudu run` can launch a CMake-backed executable target.
- CMake-backed builds compile generated per-module `.cpp` files for imported
  Dudu modules.
- `dudu build` can drive a declared target from a user-owned CMake project.
- `dudu run` can launch a declared executable from a user-owned CMake project.
- `dudu build` fails clearly when the selected backend cannot model the
  manifest.
- `duc emit` still works without a manifest.
- `duc build` still works without a manifest.
- Generated C++ remains inspectable.

## Design Guardrails

- Prefer visible native commands over hidden magic.
- Keep `dudu.toml` declarative and small.
- Let CMake remain CMake.
- Let C++ linking remain visible.
- Keep all native escape hatches available.
- Make Python-shaped workflows easy without pretending Dudu is Python.
