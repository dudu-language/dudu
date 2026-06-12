# Development Plan

The active development plan is
[Python Subset Compiler Plan](python-subset-compiler-plan.md).

Next implementation objective:

```text
Implement the first typed-Python compiler slice:
parse, typecheck, emit C++, compile, and run a tiny `.dd` program using classes,
functions, locals, returns, tuple return, and destructuring.
```

The existing C++ source contains an older emitter. The next implementation pass
should split the compiler into cohesive files and replace that parser with the
Python-shaped parser described in the active plan.
