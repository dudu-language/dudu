# C And C++ Interop

Interop is the reason this language exists.

Dudu should not ask users to abandon existing C and C++ ecosystems. The first
backend should emit readable C++ and use existing C++ build tools.

## Include Forms

Dudu source imports:

```dudu
include "math.dd"
```

C header imports:

```dudu
c include "math.h" as math
```

C++ header imports:

```dudu
cpp include "raylib.h" as rl
```

The alias is required for external headers. Imported names are accessed through
dot paths:

```dudu
math.sqrt 9.0
rl.InitWindow 800 600 "window"
```

## C Interop Rules

C interop should work first because C has a simpler ABI and fewer language
features.

The compiler should import:

- functions
- structs
- enums
- typedefs
- global constants
- simple constant-like macros

Example:

```dudu
c include "stdio.h" as c
c include "math.h" as math

fn main i32

    x f64 = math.sqrt 9.0
    c.printf "sqrt: %f\n" x
    0
```

Imported C structs behave like Dudu records:

```dudu
c include "raylib.h" as rl

fn make_pos rl.Vector2

    rl.Vector2 400 300
```

## C++ Interop Rules

C++ interop should be practical before it is complete.

The compiler should use Clang tooling to read C++ headers instead of expecting
users to hand-write every binding.

The first C++ interop layer should support:

- namespaces through dot paths
- free functions
- static constants
- enum classes
- plain structs/classes with public fields
- constructors that can be called like functions
- basic member functions

Example:

```dudu
cpp include "raylib.h" as rl

fn main i32

    rl.InitWindow 800 600 "dudu"

    while not rl.WindowShouldClose
        rl.BeginDrawing
        rl.ClearBackground rl.BLACK
        rl.DrawCircle 400 300 40 rl.RED
        rl.EndDrawing

    rl.CloseWindow
    0
```

## Overloads

C++ overload sets should resolve from Dudu argument types when possible.

```dudu
cpp include "math.hpp" as math

fn demo f32

    math.lerp 0.0 10.0 0.5
```

If multiple overloads remain valid, the compiler should require a cast or a
more explicit binding. Do not guess silently.

## Templates

Templates are not a v1 feature, but the interop plan should leave space for
them.

Possible future spelling:

```dudu
values std.vector i32
```

This could emit:

```cpp
std::vector<int32_t> values;
```

Template-heavy APIs can initially be handled by small C++ wrapper headers.

## Exceptions

Dudu should not expose C++ exceptions as a first-class language feature in v1.

For imported C++ functions that may throw, the first implementation can either:

- allow exceptions to pass through generated C++, or
- require wrapper code that converts failures to explicit status values.

This should be decided when the compiler reaches real C++ interop.

## Practical Limits

C++ has features that are difficult to expose cleanly:

- macros
- overload sets
- templates
- ADL
- exceptions
- reference qualifiers
- custom allocators
- complex operator overloads
- ABI and compiler-version differences

The first interop layer should support common, useful cases before chasing full
C++ coverage.

## Generated C++

Generated C++ should be boring:

```cpp
struct Vec2 {
    float x;
    float y;
};

int add(int a, int b) {
    return a + b;
}
```

Readable output matters because it makes debugging, profiling, and interop
failures less mysterious.
