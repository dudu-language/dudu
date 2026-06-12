# Compiler Plan

> Historical note: this plan targets the earlier low-punctuation Dudu syntax.
> The active plan is now
> [Python Subset Compiler Plan](python-subset-compiler-plan.md).

This is a staged plan. Stage 1-3 are partially implemented by the first
line-oriented C++ emitter.

## Stage 0: Spec Fixtures

- Keep example `.dd` programs in `examples/`.
- Treat the examples as parse/typecheck fixtures once a compiler exists.
- Do not implement broad features until a tiny vertical slice works.

## Stage 1: Parser

- Lex indentation, identifiers, numbers, strings, and comments.
- Parse things.
- Parse enums.
- Parse `tp` aliases.
- Parse function signatures with newline-separated arguments.
- Parse simple statements:
  - local declarations
  - `con`
  - assignment
  - calls
  - `if`
  - `while`
  - `for`
  - `break`
  - `continue`
  - `ret`

## Stage 2: Typechecker

- Built-in scalar types.
- Thing types.
- Enum types.
- `tp` aliases.
- Function signatures.
- Local variables.
- Basic expression checking.
- Block values and implicit function returns.
- Expression-valued `if` lowering.
- Basic reference/pointer spelling: `ref`, `ref mut`, `ptr`.

## Stage 3: C++ Emission

- Emit one C++ translation unit.
- Emit things, enums, aliases, and functions.
- Emit local variables, calls, loops, conditionals, and returns.
- Emit expression-valued blocks through temporaries where needed.
- Use `clang++` to build the emitted C++.

## Stage 4: C Interop

- Accept `use c`.
- Start with explicit declarations if header parsing is not ready.
- Add Clang-assisted header import once the core compiler is stable.

## Stage 5: C++ Interop

- Accept `use cpp`.
- Support namespaces and direct function/constant access.
- Add basic imported class/thing method calls.
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
