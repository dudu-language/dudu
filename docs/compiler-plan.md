# Compiler Plan

The active compiler plan is [Python Subset Compiler Plan](python-subset-compiler-plan.md).

Compiler architecture:

- lexer
- parser
- AST
- resolver
- typechecker
- C++ emitter
- runtime/prelude headers
- build driver
- formatter

The implementation target is typed Python-shaped `.dd` source that emits
readable `.cpp` and `.hpp` files.
