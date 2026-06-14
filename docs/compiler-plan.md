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

The compiler frontend should stay separate from any one backend. C++ is the
primary backend because Dudu's core promise is native-speed C/C++ interop, but a
clean frontend makes other backends possible when there is a real ecosystem
reason, such as analysis tools, documentation output, JavaScript/WASM
experiments, or Python stubs. Alternate backends must not weaken the C/C++
semantics or turn the language into a lowest-common-denominator transpiler.
