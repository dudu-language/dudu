# Native Identity Plan

Dudu native interop should not decide identity by raw C/C++ spelling. Header
spellings are useful for display and emission, but they are not stable enough
for semantic identity.

Problem cases:

- `std::string`, `std::__cxx11::basic_string<char>`, and aliases can represent
  the same useful type.
- `std::vector<int>`, `::std::vector<int>`, and imported aliases may spell the
  same declaration differently.
- two different namespaces can contain the same tail name.
- typedefs and `using` aliases can hide the canonical declaration.
- inline namespaces and implementation namespaces can leak through Clang output.
- overloaded template methods can share names but differ by canonical
  declaration and instantiated argument types.

Raw spelling must not be the primary semantic key.

## Target Model

Native declarations should carry explicit identity metadata:

```cpp
struct NativeSymbolId {
    std::string usr;
    std::string canonical_path;
};
```

The exact shape can change, but every scanned native type, function, method,
field, value, enum, namespace, and macro should have:

- a stable canonical declaration identity when Clang can provide one
- a canonical namespace path for Dudu lookup
- the imported Dudu binding path, including direct or aliased import behavior
- source location for editor navigation
- structured `TypeRef` metadata for Dudu sema
- original C/C++ spelling only for hover, diagnostics, and emission

## Rules

- Dudu source AST stays Dudu-shaped. It must not store raw native spelling as a
  normal type or expression fallback.
- Native metadata may preserve original C/C++ spellings, but only behind
  explicitly named native-boundary fields.
- Type compatibility should compare structured `TypeRef`s and native
  identities, not suffix string checks.
- Name resolution should use imported bindings and canonical native identities,
  not raw spelling equality.
- Header deduplication should prefer native identity and canonical signatures,
  not rendered strings.
- Raw spelling fallback is acceptable only for display, diagnostics, explicit
  `cpp(...)` escape handling, or a native artifact that Clang cannot model.

## Implementation Work

1. Add native identity fields to native metadata structs.
2. Populate identity fields during header scanning.
3. Preserve both canonical path and user-facing imported path.
4. Move native type/function/class maps toward identity-aware keys.
5. Replace suffix-name compatibility checks with identity-aware checks where
   metadata exists.
6. Keep a narrow diagnostic for metadata-less native artifacts instead of
   silently accepting raw spelling equality.
7. Add tests for:
   - direct and aliased imports resolving to the same native declaration
   - two namespaces with the same tail type name
   - typedef and `using` aliases
   - inline namespace spelling artifacts
   - overloaded template functions and methods
   - LSP go-to-definition through aliased native imports

## Definition Of Done

- Native identities are visible in the scanner output model.
- Normal Dudu sema does not rely on raw C++ spelling suffix checks when native
  identity metadata exists.
- Hover can still show original C++ spelling.
- Emitted C++ remains readable and valid.
- Native interop tests cover namespace/alias collisions that would fail under
  string-only identity.
