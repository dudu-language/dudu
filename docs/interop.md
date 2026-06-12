# C And C++ Interop

Interop is the reason this language exists.

Dudu should not ask users to abandon existing C and C++ ecosystems. The first
backend should emit readable C++ and use existing C++ build tools.

## C Interop

C interop should be direct and early.

Possible source shape:

```dudu
c include "math.h" as c

fn main i32

    var x f64 = c.sqrt 9.0
    ret 0
```

The compiler can map C functions, structs, enums, constants, and macros that
expand to constants or simple expressions.

## C++ Interop

C++ interop is harder than C interop, but it is the interesting target.

Possible source shape:

```dudu
cpp include "raylib.h" as rl

fn main i32

    rl.InitWindow 800 600 "window"
    ret 0
```

The compiler should use Clang tooling to read headers instead of expecting users
to hand-write every binding.

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
