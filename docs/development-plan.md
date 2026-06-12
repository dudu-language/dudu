# Development Plan

This is the near-term implementation plan for turning the first Dudu emitter
into something testable and useful.

## Current Baseline

The repo has a first line-oriented `.dd` to C++ emitter.

Validated today:

- `examples/hello.dd` emits C++ and runs through `stdio.h`.
- `examples/use_math.dd` pulls in another Dudu file and runs.
- `examples/raylib_window.dd` emits plausible C++ for a raylib-style C API.

Known limits:

- `src/main.cpp` is too large and should be split before adding much more.
- There is no real typechecker yet.
- C/C++ imports are syntactic includes plus alias stripping, not Clang-backed
  symbol import.
- External library tests are not broad enough.

## Next Goal Run

The next goal should make interop testable.

## Toolchain Direction

Dudu should follow normal C/C++ tooling instead of inventing a parallel build
world.

Pipeline:

```text
.dd source
    -> duc
    -> generated .cpp/.hpp
    -> clang++/g++/MSVC through CMake or direct compiler mode
    -> native binary/library
```

Command name:

```text
duc
```

`duc` means Dudu compiler. The language is Dudu, source files are `.dd`, and the
compiler command is `duc`.

Near-term command shapes:

```sh
duc emit src/main.dd -o build/main.cpp
duc build
duc run
duc test
```

The current `dudu` executable can be renamed to `duc` during the next goal run.

Build integration should support two modes:

- **Direct mode:** emit C++ and call `c++`/`clang++` for a simple file.
- **CMake mode:** generate or participate in a CMake build for packages and
  external libraries.

CLI should feel familiar to C/C++ users while keeping a few cargo-style
conveniences:

```sh
duc emit src/main.dd -o build/main.cpp
duc build
duc run
duc test
duc src/main.dd --emit-cpp -
```

Package metadata can start with `dudu.toml`. The name is fine for now because
the manifest describes the Dudu package, while the build contents remain normal
C/C++ include paths, libraries, flags, and CMake/pkg-config integration.

```toml
name = "my_app"
main = "src/main.dd"
cpp_std = "c++20"

[cc]
compiler = "clang++"
include_dirs = ["third_party/include"]
lib_dirs = ["third_party/lib"]
libs = ["raylib", "m"]
flags = ["-Wall"]
```

Real-library probes should prefer existing C/C++ discovery mechanisms:

- `pkg-config` for libraries like raylib or SQLite.
- CMake `find_package` where that is the normal path.
- raw include/lib/link flags as an escape hatch.

The default local test suite should use checked-in fixture libraries, but the
goal run should also add real GLM/raylib/SQLite probes. If a machine is missing
a probe dependency, the script may report that probe as unavailable, but the
project should still include those tests.

Dudu should keep generated C++ inspectable and preserve `compile_commands.json`
where possible.

## Import Path Decision

Use quoted strings for filesystem/header paths:

```dudu
use "math.dd"
use c "stdio.h" as c
use cpp "raylib.h" as rl
```

This mirrors C/C++ enough to be familiar: quoted text is a path/header spelling,
not a Dudu symbol. For generated C++, Dudu can preserve the user's header style
where possible:

```dudu
use c "local.h" as local     # emits #include "local.h"
use c "stdio.h" as c         # may emit #include <stdio.h> when configured as a system header
```

Bare module names can be a later package feature:

```dudu
use math
use glm as glm
```

Do not implement bare module imports until the package layout exists.

## Interop Strategy Decision

The next goal should use **emit-only interop plus generated C++ compilation**.

Meaning:

- Dudu emits `#include`s and C++ calls.
- The generated C++ is compiled and linked with normal C/C++ tools.
- The C++ compiler catches missing headers, bad calls, and link problems.
- Clang-backed header import/typechecking comes later.

This still supports the project goal: Dudu can use real C/C++ libraries now,
and C/C++ can use Dudu once Dudu emits `.cpp/.hpp` or libraries.

Avoid a temporary `foreign` declaration system unless the goal run gets blocked
by a concrete test that cannot be expressed otherwise.

### 1. Split The Compiler

Split `src/main.cpp` into cohesive files before expanding behavior:

- `src/source.*`: file reading, comments, indentation, source lines.
- `src/tokens.*`: word/token splitting.
- `src/ast.*`: AST structs.
- `src/parser.*`: top-level and statement parsing.
- `src/emitter.*`: C++ emission.
- `src/main.cpp`: CLI only.

Keep each file roughly within the existing `AGENTS.md` 300-500 line target.

### 2. Build A Fixture Test Layout

Add test fixtures:

```text
tests/
    fixtures/
        basic/
        interop_c/
        interop_cpp/
        language/
    expected/
```

Add a script:

```text
scripts/test.sh
```

The script should:

- build `dudu`
- emit C++ for fixtures
- compile runnable fixtures with `clang++` or `c++`
- run fixtures that do not need optional system libraries
- diff emitted C++ for fixtures where readable output matters

### 3. Add Local C Interop Fixtures

Create small local C headers/sources so interop tests do not depend on system
packages:

```text
tests/fixtures/interop_c/dudu_c_math.h
tests/fixtures/interop_c/dudu_c_math.c
```

Test cases:

- import C functions
- import C constants
- import C structs
- pass pointers
- pass `ptr const T`
- fill an output pointer
- use an imported enum

Example Dudu:

```dudu
use c "dudu_c_math.h" as cm

fn main i32

    v cm.CVec2 = cm.CVec2 3 4
    len f32 = cm.cvec2_len v
    cm.print_len len
    0
```

### 4. Add Local C++ Interop Fixtures

Create local C++ headers/sources:

```text
tests/fixtures/interop_cpp/dudu_cpp_vec.hpp
tests/fixtures/interop_cpp/dudu_cpp_vec.cpp
```

Test cases:

- import namespaces
- call free functions
- use public structs/classes
- call constructors
- call basic methods
- use static constants
- use enum classes

Example Dudu:

```dudu
use cpp "dudu_cpp_vec.hpp" as dv

fn main i32

    a dv.Vec2 = dv.Vec2 3 4
    b dv.Vec2 = dv.scale a 2
    b.length
```

### 5. Add Real Library Probes

These should exist in the repo. Test scripts can skip running a probe when the
dependency is missing on the current machine.

Candidates:

- GLM: header-only C++ math library.
- raylib: C-style graphics API.
- SQLite: C library with real pointer-heavy API.

Test detection should be explicit:

```sh
pkg-config --exists raylib
```

For GLM, compile a tiny C++ probe:

```cpp
#include <glm/glm.hpp>
int main() { glm::vec3 v{1, 2, 3}; return v.x == 1 ? 0 : 1; }
```

Do not make real-library absence fail the default local fixture suite.

### 6. Implement Enough Type Knowledge For Interop

Before Clang-backed import, maintain a small symbol table from parsed Dudu and
manual foreign assumptions:

- known thing names
- known aliases
- known enum names
- known external aliases
- known all-caps external constants

Only add explicit foreign declarations if needed:

```dudu
foreign c cm
    fn cvec2_len f32
        v cm.CVec2
```

This is a fallback if emit-only interop cannot cover an important test.

### 7. Decide Header Import Implementation

There are two viable paths:

1. **Emit-only interop first**
   - Dudu emits includes and C++ calls.
   - C++ compiler catches bad foreign calls.
   - Fastest path.

2. **Clang-backed symbol import**
   - Use libclang or Clang tooling to parse headers.
   - Dudu typechecks foreign functions/classes itself.
   - More correct, much more work.

Recommendation for next goal: keep emit-only interop, but design tests so they
compile and run the generated C++. That catches real interop breakage without
building a header importer yet.

## Language Fixtures To Add

Add fixtures for:

- `th` construction
- `tp` aliases
- `enum` values and underlying types
- `con`
- early `ret`
- final expression returns
- expression-valued `if`
- nested block calls
- `while`
- `for item in items`
- `for i in 0..count`
- pointers: `adr`, `at`, `null`
- arrays: `arr T N`, indexing
- spans

## Success Criteria

The next goal run is done when:

- compiler source is split into cohesive files
- default tests build and run
- local C interop fixture compiles and runs
- local C++ interop fixture compiles and runs
- optional GLM/raylib probes are present and skipped when unavailable
- docs show how to run all tests
- generated C++ stays readable enough to debug
