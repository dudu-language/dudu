# C And C++ Interop

The active interop plan is in
[Python Subset Compiler Plan](python-subset-compiler-plan.md).

Interop requirements:

- `import c "header.h" as alias`
- `import cpp "header.hpp" as alias`
- generated `.hpp`/`.cpp` files for C++ consumers
- generated `.h` files for `@extern_c` C ABI exports
- generated includes
- namespace/member lowering
- constructors and destructors through generated C++
- references and const-correct types
- imported templates
- function pointer types
- C ABI calls
- C++ ABI calls
- generated `.hpp` files usable from C++
- Clang-backed header import for complete library understanding

The native header scanner plan is
[Native Header Awareness Plan](header-awareness-plan.md).

Dudu code imports Dudu modules directly. Foreign headers are the boundary to
the C and C++ ecosystem, not the normal Dudu module interface.

Macros are not part of the core interop surface. Users can write wrapper
headers when macro-heavy APIs need a stable callable shape.
