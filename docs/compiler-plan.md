# Compiler Plan

This is a staged plan for implementation after the language sketch settles.

## Stage 0: Spec Fixtures

- Keep example `.glaive` programs in `examples/`.
- Treat the examples as parse/typecheck fixtures once a compiler exists.
- Do not implement broad features until a tiny vertical slice works.

## Stage 1: Parser

- Lex indentation, identifiers, numbers, strings, and comments.
- Parse structs.
- Parse function signatures with newline-separated arguments.
- Parse simple statements:
  - `var`
  - `let`
  - assignment
  - calls
  - `if`
  - `while`
  - `ret`

## Stage 2: Typechecker

- Built-in scalar types.
- Struct types.
- Function signatures.
- Local variables.
- Basic expression checking.
- Basic reference/pointer spelling: `ref`, `ref mut`, `ptr`.

## Stage 3: C++ Emission

- Emit one C++ translation unit.
- Emit structs and functions.
- Emit local variables, calls, loops, conditionals, and returns.
- Use `clang++` to build the emitted C++.

## Stage 4: C Interop

- Accept `c include`.
- Start with explicit declarations if header parsing is not ready.
- Add Clang-assisted header import once the core compiler is stable.

## Stage 5: C++ Interop

- Accept `cpp include`.
- Support namespaces and direct function/constant access.
- Add basic class/struct method calls.
- Defer complex templates and macro-heavy APIs until the basics are proven.

## Stage 6: Tooling

- Formatter.
- Diagnostics with source ranges.
- Language server basics.
- Build cache.
- Package/build integration.

## Implementation Bias

Prefer a boring compiler architecture:

- tokenizer
- parser
- AST
- name resolver
- typechecker
- C++ emitter

Do not begin with an optimizer. C++ is the optimization backend for the first
phase.
