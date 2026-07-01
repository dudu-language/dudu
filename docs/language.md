# Language

The active language spec is [Appearance Spec](appearance-spec.md).

Dudu source is typed Python-shaped code that lowers to C++.

Core forms:

- `import`
- `import module.path as alias`
- `from module.path import Name`
- `import c "header.h" as alias`
- `import cxx "header.h" as alias`
- `import cpp "header.hpp" as alias`
- `class`
- `enum`
- `type Name = Type`
- `def name(args...) -> Return:`
- typed locals: `name: Type = value`
- constants: `ALL_CAPS: Type = value`
- module values/singletons: `name: Type = value`
- pointers: `*T`
- references: `&T`
- const-qualified types: `const[T]`
- contiguous arrays: `array[T] = literal`, `array[T][N]`, `array[T][M, N]`
- array views: `items[a:b]`, `items[:]`, `matrix[row, :]`
- dynamic containers: `list[T]`, `dict[K, V]`, `set[T]`
- function pointer types: `fn(A, B) -> R`
- result types: `Result[T, E]`
- optional values: `Option[T]`

Dudu-native code uses the fixed-width scalar names above. `int`, `float`, and
`double` are C/C++ interop spellings, not Dudu source aliases.

`array[T]` without an initializer is incomplete. Use `array[T][shape]` when the
shape is not inferable from the right-hand side.

Vector-like types can opt into GLSL-style swizzling:

```python
xy = v.xy
rgb = color.rgb
v.xy = Vec2[f32](1.0, 2.0)
```

Swizzles are compile-time, type-aware member forms. Reads may repeat components
such as `v.xx`; writes may not repeat components.

Imports are qualified by default. Direct imported names must not collide unless
they are explicitly aliased.
