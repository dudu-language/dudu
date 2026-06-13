# Native Header Awareness Plan

Dudu foreign imports should feel like C/C++ includes, not like hand-written
binding files.

Today this works at C++ emission time:

```python
import c "SDL3/SDL.h" as sdl
```

The generated C++ includes `SDL3/SDL.h`, and expression lowering turns
`sdl.SDL_PollEvent` into `SDL_PollEvent`. The Dudu typechecker does not parse
the imported header, so native typedefs such as `SDL_Event` are unknown unless
the user writes:

```python
extern type SDL_Event
```

That is acceptable as an escape hatch, but it is not acceptable as the normal
interop workflow. Users should not have to know and predeclare every imported
type by hand.

## Target Behavior

This should typecheck without manual declarations:

```python
import c "SDL3/SDL.h" as sdl

def pump_events() -> bool:
    event: SDL_Event
    while sdl.SDL_PollEvent(&event):
        if event.type == sdl.SDL_EVENT_QUIT:
            return False
    return True
```

Generated C++ should remain ordinary C++:

```cpp
#include "SDL3/SDL.h"

bool pump_events() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
    }
    return true;
}
```

## Tooling Policy

Dudu should require Clang tooling for automatic native header awareness.

The generated C++ compiler remains configurable. Users can still compile with
GCC, Clang, MSVC, or another C++ compiler. Clang is required for the separate
job of understanding imported C/C++ headers because Clang exposes a stable
tooling API and AST model; GCC does not provide an equivalent practical API for
this use case.

Policy:

- `duc check`, `duc emit`, `duc build`, and editor diagnostics require Clang
  tooling when a source file imports C/C++ headers and uses imported native
  declarations that Dudu must understand.
- Missing Clang tooling should be a clear setup error, not a silent downgrade.
- `extern type` remains available for unusual generated/platform headers and
  quick experiments, but docs and examples should not rely on it for normal
  SDL, imgui, raylib, OpenCL, Vulkan, POSIX, or standard-library usage.

Suggested diagnostic:

```text
dudu: src/main.dd:7:12: native header awareness requires clang tooling
  import c "SDL3/SDL.h" as sdl
  install clang/libclang, or declare an explicit escape hatch:
      extern type SDL_Event
```

## Implementation Shape

Add a native-header scanner stage before semantic body checks:

```text
parse .dd
load Dudu modules
collect foreign imports and build flags
scan native headers with Clang tooling
merge discovered declarations into Dudu symbols
semantic check
emit C++
build with configured C++ compiler
```

The first implementation should be a separate component, not mixed into parser
or emitter code:

```text
src/dudu/native_headers.hpp
src/dudu/native_headers.cpp
src/dudu/native_header_cache.hpp
src/dudu/native_header_cache.cpp
```

The scanner input should include:

- header spelling from `import c "..." as name` and `import cpp "..." as name`
- import kind, C or C++
- include directories from `dudu.toml`
- defines and compile flags from `dudu.toml`
- pkg-config cflags from `dudu.toml`
- C++ standard
- target mode where relevant

## First Useful Slice

Start with types only. This removes the biggest current usability problem
without requiring full overload/type modeling.

Discover and register:

- typedef names
- using aliases
- structs and classes
- unions
- enums and enum constants

Dudu should add discovered type names to the same symbol table path used by
`extern type`, but mark them as native-discovered so diagnostics can explain
their origin.

This is enough for:

```python
event: SDL_Event
window: *SDL_Window = None
renderer: *SDL_Renderer = None
```

and for many OpenCL, Vulkan, POSIX, FFmpeg, SQLite, and GLFW handle types.

## Second Slice

Add native values and constants:

- enum constants
- object-like macros with simple literal values
- global variables where present

This makes C imports feel less magical because constants in headers become
known symbols instead of only passing through to generated C++.

Macro policy should stay conservative. Function-like macros are not normal
functions. Prefer wrapper headers for macro-heavy APIs.

## Third Slice

Add functions:

- function names
- parameter types
- return types
- C varargs marker
- C++ namespace-qualified functions
- simple overload sets

This enables better diagnostics:

```text
no matching overload for ImGui.SliderFloat(str, *i32, f32, f32)
candidate expects *f32 for parameter 2
```

It also allows Dudu to infer return types from imported calls instead of
falling back to loose expression typing.

## Fourth Slice

Add C++ classes and richer constructs:

- constructors
- destructors
- methods
- fields
- nested types
- templates with explicit arguments
- overloaded operators
- references and const correctness

This is the real C++ interop milestone. It should be built after the type and
function scanner paths are stable.

## Cache

Header scanning must be cached.

Cache key inputs:

- resolved header path
- import kind
- compiler resource directory or Clang version
- include directories
- defines
- flags that affect parsing
- pkg-config cflags
- C++ standard
- target triple when configured
- mtimes or content hashes for directly included headers

Cache output:

- discovered types
- discovered constants
- discovered functions
- diagnostics from Clang

Default location:

```text
build/dudu-header-cache/
```

Provide a way to clear it:

```sh
duc clean-cache
```

or:

```sh
duc check --clear-header-cache
```

## CLI And Project Config

Use the existing C/C++ project config as scanner input:

```toml
cpp_std = "c++20"

[cc]
include_dirs = ["third_party/install/include"]
defines = ["IMGUI_DISABLE_OBSOLETE_FUNCTIONS"]
flags = ["-fPIC"]

[pkg_config]
packages = ["sdl3"]
```

Add explicit scanner configuration only where needed:

```toml
[native_headers]
clang = "clang++"
cache_dir = "build/dudu-header-cache"
```

Do not make users duplicate include paths in a second config section.

## Diagnostics

Header diagnostics should point at both Dudu source and native tooling output.

Examples:

```text
dudu: src/main.dd:1:10: could not scan native header SDL3/SDL.h
  import c "SDL3/SDL.h" as sdl
clang++: fatal error: 'SDL3/SDL.h' file not found
hint: add include_dirs or pkg_config packages in dudu.toml
```

```text
dudu: src/main.dd:8:12: unknown native type SDL_Event
  event: SDL_Event
hint: SDL3/SDL.h was not scanned; native header awareness requires clang tooling
```

## Tests

Add fixture headers so the core test suite does not depend on system SDKs:

```text
tests/fixtures/native_headers/simple_c.h
tests/fixtures/native_headers/simple_cpp.hpp
tests/fixtures/native_headers/macros.h
```

Core tests:

- C typedef struct
- C typedef union
- C enum and enum constants
- opaque struct pointer
- C++ namespace function
- C++ class type
- include dirs from `dudu.toml`
- pkg-config cflags when available
- cache hit/miss behavior
- clear diagnostic when Clang tooling is missing

Optional probes:

- SDL3 `SDL_Event`
- imgui backend headers
- raylib
- OpenCL
- Vulkan
- GLFW
- FFmpeg

## Migration

After the type-only scanner works:

- remove `extern type SDL_Event` from the SDL3/imgui playground
- keep one small `extern type` fixture as an escape-hatch test
- update docs so `extern type` is described as manual override, not normal API
- add a note that automatic native interop requires Clang tooling

