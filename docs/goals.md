# Project Goals

Dudu is a statically typed Python subset that compiles to readable C++.

The goal:

```text
99% Python-shaped syntax.
C/C++ capability.
Readable generated .hpp/.cpp files.
Direct access to C and C++ libraries.
No CPython runtime dependency.
```

Authoritative docs:

- [Appearance Spec](appearance-spec.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

The compiler should support practical systems programming: native values,
pointers, references, fixed-width integers, fixed arrays, dynamic containers,
manual allocation, C/C++ imports, generated headers, hardware layout controls,
atomics, volatile memory, target attributes, and build modes for hosted,
freestanding, embedded, CUDA, and shader-style targets.
