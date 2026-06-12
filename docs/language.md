# Language

The active language spec is [Appearance Spec](appearance-spec.md).

Dudu source is typed Python-shaped code that lowers to C++.

Core forms:

- `import`
- `import module.path as alias`
- `from module.path import Name`
- `import c "header.h" as alias`
- `import cpp "header.hpp" as alias`
- `class`
- `enum`
- `type Name = Type`
- `def name(args...) -> Return:`
- typed locals: `name: Type = value`
- constants: `ALL_CAPS: Type = value`
- pointers: `*T`
- references: `&T`
- const-qualified types: `const[T]`
- fixed arrays: `T[N]`
- dynamic containers: `list[T]`, `dict[K, V]`, `set[T]`
- function pointer types: `fn(A, B) -> R`
- result types: `Result[T, E]`
- optional values: `Option[T]`

Imports are qualified by default. Direct imported names must not collide unless
they are explicitly aliased.
