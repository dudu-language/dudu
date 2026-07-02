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
whether they drive generated CMake or a user-owned CMake project is a backend
choice. Direct compiler builds stay available through `duc build <file.dd>` for
low-level compiler-driver smoke/debug work, but they are not a Dudu project
backend.

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
CMakeLists.txt
```

`dudu init tools/pack` initializes a specific directory, creating it if needed.

`dudu new hello` creates a new directory:

```text
hello/
    dudu.toml
    src/main.dd
    README.md
    CMakeLists.txt
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
- Explicit local Dudu and native imports such as `import math` and
  `from cpp.path import local.hpp` are relative to the source file that wrote
  them.
- Native/system imports such as `from cpp import raylib.h` search configured
  include paths, pkg-config include paths, and toolchain include paths.
- Running `dudu` from another working directory must not change behavior when
  it finds the same manifest.

Status: project-driver entry and target resolution uses manifest-relative paths
for default entries, named targets, and project-directory build/run/CMake
commands. This is covered by project-config tests and a command-level
`dudu build <project-dir>` smoke.

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
dudu run -- --user-flag value
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
- `dudu run ... -- args...` forwards trailing arguments to the executable.
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

## Dudu Dependencies And Lockfiles

Dudu does not have a full package manager yet, but real libraries such as
`ndad` will need a reproducible dependency story. The source language should
keep imports clean:

```python
from ndad import Tensor
from ndad.backends import openblas
```

Source imports should not contain remote Git URLs. Dependency resolution belongs
in project metadata and lock state, not scattered through `.dd` files.

Cargo and Go both support Git/VCS-backed dependency workflows, but they make
different tradeoffs. Cargo allows Git dependencies in `Cargo.toml` and records
resolved versions in `Cargo.lock`. Go module paths are import-like paths and
versions are resolved through `go.mod`/`go.sum` using module metadata and VCS
tags. Both systems separate source import spelling from reproducibility state.

Dudu's eventual package/dependency model should follow that separation:

```toml
[deps]
ndad = { git = "https://github.com/wefafwefgawefg/ndad.git", tag = "v0.1.0" }
mald = { git = "https://github.com/wefafwefgawefg/mald.git", rev = "..." }
local_math = { path = "../local_math" }
```

The lockfile is the generated reproducibility record. It pins the exact
resolved dependency identities, usually commit hashes and package metadata, so
`dudu build` on another machine uses the same source revision instead of "the
latest thing on that branch." It is analogous to `Cargo.lock` or `go.sum` in
purpose, even if the exact format differs.

Important boundaries:

- Dudu package deps are source-level Dudu dependencies.
- Native C/C++ deps remain native deps: CMake packages, pkg-config packages,
  system packages, vendored sources, or user-owned CMake logic.
- A Dudu package may declare native requirements, but Dudu should not promise
  portable binary dependency management for C/C++ libraries.
- `dudu build` should be able to fetch or locate Dudu source deps before
  running the generated/user-owned CMake backend.
- `duc` should stay usable without a package manifest for explicit compiler
  workflows.

Reasons to do this:

- users can depend on early Dudu libraries before a central registry exists
- dogfood libraries such as `ndad` can be pinned by Git tag or commit
- source imports stay stable if a dependency moves from Git to a future
  registry
- CI and collaborators get reproducible builds

Reasons to be cautious:

- package resolution can grow into a second build system if it tries to own
  native dependencies
- private repos, auth, mirrors, and offline builds complicate UX
- Dudu's native interop promise means package metadata must cooperate with
  CMake/pkg-config rather than replacing them
- lockfile churn and multi-target native platform differences need clear rules

Near-term alternative if this is not implemented yet:

- document dependencies in `README.md`
- vendor Dudu libraries as submodules or sibling checkout paths
- use scripts to clone expected repos
- configure include/source paths in `dudu.toml` or user-owned CMake

That is acceptable for dogfood, but it is not a good long-term story for a
language people should be able to try quickly.

Status: the first dependency bootstrap is implemented. `dudu.toml` supports:

```toml
[deps]
local_math = { path = "../local_math" }
ndad = { git = "https://github.com/wegfawefgawefg/ndad.git", tag = "v0.1.0" }
```

`dudu deps fetch` resolves deps and writes `dudu.lock`. Normal project-driver
commands resolve missing deps before indexing/building. Source imports remain
logical:

```python
from local_math import value
from ndad import Tensor
```

Path deps resolve relative to the manifest. Git deps are cloned under
`.dudu/deps/<name>` and pin the resolved commit in `dudu.lock`; the cache
directory is ignored, while the lockfile is intentionally not ignored. A Dudu
source dependency must resolve to a package root containing `dudu.toml`; `src/`
is used as the package module root when it exists. Native C/C++ dependencies
still belong to CMake/pkg-config/system/vendor setup, not the Dudu source
dependency lockfile.

## Build Backends

`dudu build`, `dudu run`, and `dudu test` are the stable front door for Dudu
projects. Backend choice is an implementation detail, not a split between
toy and real workflows.

This is the serious model for native projects. Users should not have to leave
`dudu build` when a project becomes real. The driver may choose a generated
CMake backend or a user-owned CMake backend, but the user-facing command
remains the same. Direct compiler builds remain a `duc` compiler-driver
convenience, not a `dudu` project mode.

This is not Dudu trying to replace CMake or clone another language's build
tool. Dudu's native interop promise means it must cooperate with existing C and
C++ build systems. The stable contract is the Dudu command surface; the backend
is how that command gets honest native build work done.

The build-driver contract is:

- `dudu.toml` is the canonical Dudu project file. It owns Dudu entries,
  targets, generated artifact locations, Dudu compile-time settings, test/bench
  delegation, and the selected native backend.
- `dudu.toml` is not required to be the canonical source of every native
  compilation detail. For projects that already have real CMake machinery,
  CMake may remain the authority for native target definitions, platform
  conditionals, generator choices, toolchain files, install rules, vendored
  native dependencies, and other C/C++ build-system concerns.
- This is intentional. Forcing every C/C++/CUDA/Objective-C/platform-SDK build
  rule through a Dudu manifest would mean slowly recreating a large part of
  CMake, and would make Dudu worse at its core promise: sitting cleanly on top
  of the existing C/C++ ecosystem.
- The CMake backend is the broad native-ecosystem backend. It should support
  projects that need CMake package discovery, generated build files, IDE
  integration, platform generators, and larger native dependency graphs.
- `duc build <file.dd>` can still emit one compatibility C++ translation unit
  and invoke the configured native compiler for compiler-driver smoke/debug
  work. This path is intentionally outside project manifests.
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

- If no backend is explicitly selected, `dudu build`, `dudu run`, and
  `dudu test` select the generated CMake backend so generated `.hpp/.cpp`
  artifacts stay per-module. This is true even for simple single-module
  projects, because separate generated files are the normal Dudu build model.
- `duc emit-modules` writes generated `.hpp/.cpp` artifacts per Dudu module
  plus a shared `dudu_runtime.hpp`.
- The generated CMake backend uses `duc emit-modules` and compiles the
  generated per-module `.cpp` files.
- Generated CMake module emission depends on both the loaded `.dd` module
  sources and the parsed `dudu.toml`, so build values, target mode, and other
  manifest-only Dudu settings can invalidate the emit step.
- `dudu test` can use the generated CMake backend. It uses
  `duc emit-test-modules` to emit per-module test-mode `.hpp/.cpp` artifacts
  plus a small generated `test_harness.cpp`; generated module sources suppress
  normal executable entry points and headers expose test functions to the
  harness.
- `dudu build`, `dudu run`, and `dudu test` always use CMake-backed project
  builds. The generated CMake path emits an internal CMake project and drives
  `cmake -S/-B` plus `cmake --build`. `[build] backend` is a stale manifest key
  and must be rejected instead of silently becoming arbitrary build metadata.
- The generated-CMake backend supports useful native inputs: include paths,
  library paths, libraries, compile flags, link flags, pkg-config packages, and
  extra C/C++ sources. Fixtures cover linking an extra C source into both the
  app target and generated Dudu test harness.
- `dudu cmake` emits CMake for inspection or handoff.
- User-owned CMake project integration is implemented for `dudu build`,
  `dudu run`, and `dudu test` when `[cmake] source` selects an existing CMake
  source tree. The driver configures/builds the declared `[cmake] target` under
  the configured Dudu build directory, and `dudu test` runs CTest when there is
  no Dudu test entry or explicit delegated test command.
- `dudu build`, `dudu run`, and `dudu test` stream native compiler, CMake, and
  CTest output by default through the project-driver front door. Full native
  command lines stay behind `--verbose`, generated/user-owned CMake builds
  report their CMake/build/output stages, and `--quiet` suppresses
  project-driver progress output for scripts.
- Generated and user-owned CMake backends cache the configure command and skip
  redundant `cmake -S/-B` configure runs when the CMake cache, generated
  source, and command line are unchanged. The driver still prints the
  `configure` stage so build progress remains readable, then lets
  `cmake --build` do the incremental native compile.
- `dudu check` prints `check` and `ok` progress lines on success unless
  `--quiet` is set, so successful validation is visible to humans without
  changing `duc check` script behavior.
- `dudu fmt` with no input formats the project tree from the current directory
  while skipping generated/build directories such as `.git`, `build`, and the
  configured `[build] dir`. `dudu fmt path/to/file.dd` formats that file in
  place without printing the formatted source to stdout. `duc fmt <file>`
  remains the direct formatter that prints formatted text unless an explicit
  output is requested.
- Regression coverage locks in the project-driver defaults for `dudu fmt`,
  `dudu check`, and `dudu build <project-dir>` so those front-door commands do
  not drift back toward direct compiler-driver behavior.
- `dudu bench` is documented in help. `dudu bench compiler` runs the
  compiler-throughput benchmark from a checkout that contains
  `scripts/bench_compiler.sh`. `--quiet`, `--verbose`, `--help`, and
  `--version` are parsed as project-driver flags; benchmark command arguments
  that look like flags can still be passed after `--`. The compiler benchmark
  supports scalable generated-source runs, for example
  `dudu bench compiler -- --line-scales 10000,50000,100000`.
- Delegated `[test]` and `[bench]` commands, plus fallback `scripts/test.sh`
  and `scripts/bench.sh`, execute from the manifest directory so running Dudu
  from a project subdirectory does not change command-relative paths.
- Quoted manifest strings decode common TOML escapes such as `\"`, `\\`, and
  `\n`, including inside string arrays, so command strings and native paths do
  not leak raw escape backslashes into the driver. Invalid or unfinished
  quoted-string escapes are rejected as manifest errors instead of being
  guessed.
- The stale `[cmake] enabled` and `[build] backend` manifest keys have been
  removed. User-owned CMake behavior is selected through `[cmake] source` and
  `[cmake] target`; `dudu cmake` emits an inspectable generated CMake artifact.

Future build-driver work should keep that front-door contract intact: users
should still type `dudu build`, `dudu run`, and `dudu test`.

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
broader in a different direction: existing CMake projects and generated CMake
projects are the valid ways for the Dudu project command surface to reach the
C/C++ ecosystem. Direct compiler builds remain available through `duc` for
low-level/debug use.

Put differently: Dudu is not copying Zig here. There is no plan where Dudu
tries to make every project express its whole native build as Dudu code and
then grudgingly "falls back" to CMake. CMake is one of the honest native
backends because a huge amount of C and C++ code already speaks CMake.

Dudu should not make CMake use feel like leaving the language tool. If a
project already has a serious CMake build, `dudu build` should be able to drive
that declared target. If a project does not, `dudu build` should generate and
drive CMake internally. In all cases, the user command remains Dudu's command.

The practical authority split is:

- Dudu owns Dudu semantics: source files, selected entry, generated module
  artifacts, compiler flags that affect Dudu lowering, Dudu tests, Dudu
  formatting, and Dudu diagnostics.
- CMake may own native semantics: exact compiler/linker invocations, toolchain
  files, native package discovery, target properties, install/export rules,
  platform-specific conditionals, and non-Dudu generated sources.
- `dudu build` owns orchestration: choose the backend from `dudu.toml`, emit
  Dudu artifacts, run the selected native build, stream useful progress, and
  fail clearly when the selected backend cannot represent the manifest.

That means Dudu does not need to become the single canonical compilation config
for every language in a mixed project. It does need to remain the canonical
front door for Dudu users, and it needs to make the handoff to native build
authority explicit rather than magical.

Generated-file ownership must be strict:

- In generated-CMake mode, Dudu owns the internal CMake project under the Dudu
  build directory and may overwrite it on every build. Users should not edit
  files under `build/cmake-backend/source`.
- In user-owned CMake mode, Dudu must not edit, patch, or rewrite the user's
  `CMakeLists.txt`, and it must not maintain a magic commented block inside
  that file.
- User-owned CMake integration should happen through separate generated
  artifacts with stable paths: generated `.hpp/.cpp` files plus, if useful, a
  small generated include file such as `dudu_sources.cmake` that defines
  variables or targets for the user's CMake to consume.
- `dudu init` may create an initial `CMakeLists.txt` for a new project, but
  after creation that file belongs to the user. Subsequent `dudu build` runs
  must not mow it over. The starter CMake should compile all generated Dudu
  module `.cpp` files, not only the entry module, so adding a normal imported
  `.dd` file does not require hand-editing the starter immediately. Its
  build-time refresh uses a stamp output so no-op CMake builds do not rerun
  Dudu emission.

The practical target is:

- Normal Dudu projects use generated CMake by default, even when they are
  small.
- `duc build` uses the same generated-CMake backend. Low-level `duc` remains
  useful for check/format/emit/debugging actions, but there is no separate
  direct native build backend.
- Backend selection is intentionally not a separate compiler layer anymore.
  The manifest parser rejects `[build] backend`; generated/user-owned CMake
  code owns the build path directly.
- Existing CMake projects can remain user-owned; Dudu should drive the declared
  CMake target instead of pretending to understand every project-specific build
  rule itself.
- `dudu cmake` remains useful because users can inspect, edit, or hand off the
  generated CMake artifact when that is the right native workflow. It does not
  change the normal Dudu command surface.

Project backend rules should be boring:

- If `[cmake] source` is set, drive the declared user-owned CMake project.
- If `[cmake] source` is not set, use generated CMake for `dudu`
  project-driver builds.
- If `[build] backend` is present, reject it as stale configuration.
- If the selected backend cannot model the project, fail with a diagnostic that
  names the missing capability and the backend that can support it.
- Do not silently ignore include paths, link inputs, generated files, native
  sources, package discovery, or user-owned build-system settings.

The backend is allowed to be visible in logs and artifacts. It is not allowed to
change the user's main workflow from `dudu build`, `dudu run`, and `dudu test`.

Implementation checklist:

- generated CMake backend: emit an internal CMake project and drive it from
  `dudu build`, `dudu run`, and `dudu test`
- user-owned CMake backend: configure/build/run declared targets from an
  existing CMake project
- diagnostics: reject unsupported backend/manifest combinations loudly
- logs: show which backend ran and where generated artifacts landed

Planned manifest shape:

```toml
[build]
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
6. Route `duc build`/`duc run` through the generated-CMake backend too.
7. Add pkg-config include/link flag expansion.
8. Add C and C++ source file passthrough.
9. Implement `dudu cmake`.
10. Add named targets.
11. Add `dudu test`.
12. Add `dudu new <name>`.
13. Update examples to prefer `dudu run` where it improves usability.
14. Keep `duc` workflows documented as the transparent fallback.
15. Add `dudu clean`.
16. Reject stale `[build] backend` manifest entries.
17. Add a generated-CMake path behind `dudu build`, `dudu run`, and
    `dudu test`.
18. Support user-owned CMake projects behind the same
    commands. Build/run/test are implemented for declared executable targets
    and CTest-enabled projects.
19. Keep `dudu cmake` as artifact emission for inspection and handoff, not as
    the required workflow for serious native projects.
20. Add CMake install rules and `scripts/install-local.sh` so source checkouts
    can install `dudu`, `duc`, docs, and editor support locally without package
    manager integration.

## Acceptance Tests

- `dudu init` creates a runnable hello project.
- `dudu run` builds and runs hello from a clean directory.
- `dudu run` prints the generated CMake path, build directory, and run command.
- `dudu build` can compile a project with a hand-written `.cpp` file.
- `dudu build` can compile a project using a pkg-config package such as raylib.
- `dudu cmake` emits a readable CMake project.
- `dudu build` can use the CMake backend explicitly.
- `dudu run` selects the generated CMake backend automatically for projects
  with no explicit backend setting.
- `dudu run` can launch a CMake-backed executable target.
- CMake-backed builds compile generated per-module `.cpp` files for imported
  Dudu modules.
- `dudu build` can drive a declared target from a user-owned CMake project.
- `dudu run` can launch a declared executable from a user-owned CMake project.
- `dudu test` can run CTest for a user-owned CMake project.
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
