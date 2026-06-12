# Project Goals

Glaive is a small systems language experiment.

The central question:

> Can a language use Python-like visual simplicity while compiling to native
> C++-class code and interoperating directly with existing C and C++ libraries?

## Non-Goals

- Do not preserve Python runtime semantics.
- Do not make every value a boxed dynamic object.
- Do not require garbage collection for ordinary code.
- Do not add Rust-style ownership or lifetime rules.
- Do not hide C/C++ costs behind surprising language magic.
- Do not start by inventing a full custom backend.

## Design Goals

- Low punctuation.
- Indentation-based blocks.
- Static types with inference where it stays obvious.
- C/C++-style performance model.
- C ABI support from the start.
- Practical C++ interop as a first-class direction.
- Readable generated C++ as the first backend.
- Build and debug with ordinary C++ toolchains.
- Keep the language small enough to understand.

## Intended Feel

Glaive should feel closer to this:

```glaive
fn add i32
    a i32
    b i32

    ret a + b
```

than this:

```cpp
auto add(int a, int b) -> int {
    return a + b;
}
```

or this:

```python
def add(a: int, b: int) -> int:
    return a + b
```

The language should be light to read without becoming dynamically typed or
runtime-heavy by default.

## First Milestone

The first real compiler milestone should be a tiny vertical slice:

- Parse `.glaive` files.
- Typecheck structs, functions, variables, loops, calls, and `ret`.
- Emit readable C++.
- Compile that C++ with Clang.
- Call a small C library or raylib example.

No macro system, package manager, optimizer, or advanced C++ interop is needed
for the first milestone.
