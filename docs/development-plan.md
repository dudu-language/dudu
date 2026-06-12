# Development Plan

The active development plan is
[Python Subset Compiler Plan](python-subset-compiler-plan.md).

Current implementation baseline:

```text
The compiler parses Python-shaped `.dd` source, checks the core static subset,
emits readable C++20, emits importable headers, supports multi-file Dudu
imports, validates canonical examples, and builds both `dudu` and `duc`
entrypoints.
```

Implementation work stays organized around cohesive frontend files:

- lexer and parser
- semantic analysis
- module loading
- C++ type lowering
- C++ expression lowering
- C++ statement emission
- C++ source/header emission
- formatter
- CLI/build driver
