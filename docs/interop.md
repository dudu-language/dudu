# C And C++ Interop

The active interop plan is in
[Python Subset Compiler Plan](python-subset-compiler-plan.md).

Interop requirements:

- `import c "header.h" as alias`
- `import cxx "header.h" as alias`
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

Foreign import modes are explicit because C and C++ headers need different
generated include shapes:

- `import c "header.h" as alias` scans C-style globals and emits an
  `extern "C"` include block for C headers that need C linkage in generated C++.
- `import cxx "header.h" as alias` scans C-style globals but emits a plain
  C++ include for C API headers that manage their own linkage internally.
  Libxml2-style headers use this path.
- `import cpp "header.hpp" as alias` imports C++ namespace, class, overload,
  member, and template semantics.

The quoted path is the C/C++ header spelling. It is not a Dudu module path.

Known imported macros are callable when the scanner can determine their arity.
Variadic macros enforce their fixed leading parameters and pass through extra
arguments.
Aliased imports expose lowercase function-like macros too:

```python
import c "assert.h" as cassert

def hot(x: i32):
    cassert.assert(x > 0)
```

Direct imports keep object-like macro exposure conservative. Function-like
macros are still callable with call syntax, so `assert expr` remains Dudu
syntax while `assert(expr)` can call a native macro.

See `examples/macro_bomb.dd` for a compact imported-macro stress example.
