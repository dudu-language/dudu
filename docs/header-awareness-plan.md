# Native Header Awareness Plan

Dudu foreign imports should feel like C/C++ includes, not like hand-written
binding files.

This works at C++ emission time:

```python
import c "SDL3/SDL.h" as sdl
```

The generated C++ includes `SDL3/SDL.h`, and expression lowering turns
`sdl.SDL_PollEvent` into `SDL_PollEvent`. Dudu also runs a Clang-backed native
header scan before semantic body checks, so native typedefs such as `SDL_Event`
are available without hand-written declarations.

Manual native declarations are still available as an escape hatch:

```python
type SDL_Event
```

That is not the normal interop workflow. Users should not have to know and
predeclare every imported type by hand.

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

One physical C/C++ header import should also expose all relevant symbol kinds
from that header. A native import without `as` behaves like a normal C/C++
include: discovered names enter the current Dudu module directly.

Dear ImGui is the motivating case:

```python
import cpp "imgui.h"

def init_imgui():
    IMGUI_CHECKVERSION()
    ImGui.CreateContext()
    ImGui.StyleColorsDark()
```

Generated C++ should still look like normal ImGui C++:

```cpp
#include "imgui.h"

void init_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
}
```

Dudu should not require both `import c "imgui.h" as imgui` and
`import cpp "imgui.h" as ImGui` for the same header. The scanner should attach
object-like macros, global values/functions, and C++ namespaces to the same
imported header model.

`as` is still useful when the user wants hygiene:

```python
import cpp "windows.h" as win

win.CreateWindowExA(...)
```

Direct native imports must be conservative about collisions. If a discovered
direct-import name conflicts with a Dudu declaration or another direct native
import, Dudu should emit an error and tell the user to add `as` to one import.
Do not silently shadow native names.

## Tooling Policy

Dudu should require Clang tooling for automatic native header awareness.

The generated C++ compiler remains configurable. Users can still compile with
GCC, Clang, MSVC, or another C++ compiler. Clang is required for the separate
job of understanding imported C/C++ headers because Clang exposes a stable
tooling API and AST model; GCC does not provide an equivalent practical API for
this use case.

Policy:

- `duc check`, `duc emit`, `dudu build`, and editor diagnostics require Clang
  tooling when a source file imports C/C++ headers and uses imported native
  declarations that Dudu must understand.
- Missing Clang tooling or missing project-relative imported headers are clear
  setup errors, not silent downgrades.
- `type Name` remains available for unusual generated/platform headers and
  quick experiments, but docs and examples should not rely on it for normal
  SDL, imgui, raylib, OpenCL, Vulkan, POSIX, or standard-library usage.

Suggested diagnostic:

```text
dudu: src/main.dd:7:12: native header awareness requires clang tooling
  import c "SDL3/SDL.h" as sdl
  install clang/libclang, or declare an explicit escape hatch:
      type SDL_Event
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

## Implemented Slice

Dudu currently discovers and registers:

- typedef names and using aliases
- structs and classes
- unions
- enums and enum constants
- object-like macros as native values
- function-like macros by arity
- global functions, parameter types, return types, and varargs markers
- simple pointer and reference parameter/field types
- C++ namespace-qualified functions
- simple native overload sets by arity and assignability
- C++ class fields, methods, and non-default constructor signatures
- imported C++ base classes for inherited method lookup
- derived-to-base pointer/reference assignment for imported C++ classes

Discovered type names enter the same symbol path used by manual `type Name`
declarations. Direct imports also expose native values, macros, functions,
namespaces, and class shapes. Aliased imports keep the old hygienic lowering
style while still making imported types visible.

This is enough for:

```python
event: SDL_Event
window: *SDL_Window = None
renderer: *SDL_Renderer = None
```

and for many OpenCL, Vulkan, POSIX, FFmpeg, SQLite, and GLFW handle types.

## Macro Policy

Macro policy should stay permissive for consumption and conservative for
understanding. Dudu should not add C macro-definition syntax. Instead:

- known object-like macros can be used as native passthrough expression names
- known function-like macros can be called when their arity is known
- variadic function-like macros enforce fixed leading args and pass through
  extra args
- aliased imports expose lowercase function-like macros, such as
  `cassert.assert(expr)`
- direct imports keep object-like macro exposure conservative; function-like
  macros remain callable with call syntax, such as `assert(expr)`
- unknown all-caps native-looking names from imported headers may be emitted as
  passthrough with a warning rather than blocking the user

The scanner does not need to model macro bodies. It only needs enough metadata
to preserve source spelling and avoid obvious arity mistakes. For example,
`IMGUI_CHECKVERSION()` can lower directly to `IMGUI_CHECKVERSION();`. Prefer
wrapper headers for macro-heavy APIs that cannot be represented cleanly.

## Native Function Diagnostics

Native function metadata enables better diagnostics:

```text
no matching overload for ImGui.SliderFloat(str, *i32, f32, f32)
candidate expects *f32 for parameter 2
```

It also allows Dudu to infer return types from imported calls instead of
falling back to loose expression typing.

## Remaining Deep C++ Work

Richer constructs still need deeper modeling:

- overloaded operators
- deeper references and const correctness
- destructor semantics

The scanner now preserves namespace and class scope for nested types such as
`outer_namespace.Outer.Inner`. Simple imported C++ function templates with a
single `T` parameter typecheck through explicit Dudu calls such as
`native.identity[i32](value)`. Template-heavy libraries may still need wrapper
headers.

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
[cxx]
standard = "c++20"

[include]
paths = ["third_party/install/include"]

[cc]
defines = ["IMGUI_DISABLE_OBSOLETE_FUNCTIONS"]
flags = ["-fPIC"]

[pkg]
libs = ["sdl3"]
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

Current migration state:

- `type SDL_Event` has been removed from the SDL3/imgui playground
- keep one small `type Name` fixture as an escape-hatch test
- docs describe `type Name` as manual override, not normal API
- automatic native interop requires Clang tooling
- missing project-relative native headers fail at the import with a `could not
  scan native header` diagnostic and an include/pkg hint
- broken `CLANGXX` or missing Clang tooling fails with a native-header scanner
  diagnostic and a Clang setup hint
