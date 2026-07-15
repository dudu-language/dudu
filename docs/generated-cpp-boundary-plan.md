# Generated C++ Boundary Plan

## Purpose

Dudu's generated module boundaries must behave like ordinary well-formed C++
boundaries. A shared runtime header may provide Dudu runtime facilities. It may
not become a project-wide bucket for application imports, native macros, or
module implementation details.

This is correctness work first and performance work second. Accidental
transitive includes can hide missing dependencies, leak macros and compiler
state across otherwise unrelated modules, and permit generated declarations to
drift between headers and sources.

## Confirmed Problems

1. **Project native imports leak into `dudu_runtime.hpp`.**

   `runtime_header()` receives the merged project AST and `emit_includes()`
   emits every merged C and C++ import. One module importing SDL, Vulkan,
   raylib, or another native header therefore makes that header visible to
   every generated translation unit.

2. **The runtime header is not a stable runtime boundary.**

   It mixes Dudu runtime support, standard-library dependencies, build values,
   target macros, and application-native imports. Its content changes when an
   unrelated module changes imports, so it is unsuitable as a stable shared
   header or precompiled-header boundary.

3. **Generated sources do not include their own generated headers.**

   A module `.hpp` and `.cpp` independently emit declarations. The C++ compiler
   therefore does not verify that the implementation was compiled against the
   public declaration consumed by other translation units. Divergent emission
   can become an ODR or ABI bug instead of an immediate compile error.

4. **Generated headers include more dependencies than their public interface
   requires.**

   Module includes are currently based on import statements rather than on the
   declarations exposed by the generated header. A body-only dependency should
   remain in the `.cpp`; a type used by a public field, base, parameter, or
   return type must be available from the `.hpp`.

5. **Accidental transitive includes can make invalid modules compile.**

   A module may omit a required direct import and still compile because another
   module placed the native header in `dudu_runtime.hpp`. Removing or reordering
   an unrelated import can then break it.

6. **Native preprocessor state leaks project-wide.**

   Native headers may define macros, alter packing, select platform APIs, emit
   pragmas, or depend on include order and compile definitions. Putting them in
   the runtime header applies that state to every generated module and makes
   otherwise independent imports interact.

7. **One native-header edit invalidates the whole generated project.**

   Because every translation unit includes the contaminated runtime header,
   changing any native dependency can rebuild all Dudu modules. It also
   invalidates any precompiled runtime header, erasing the intended incremental
   build benefit.

8. **Runtime support is unconditional.**

   Minimal programs receive hosted I/O, Result, tuples, containers, array views,
   indexing, shader macros, and all 22 associated standard headers. This is not
   a module-boundary correctness bug, but it shares the same remedy: explicit,
   feature-owned support headers instead of a universal textual prelude.

9. **Self-contained and module emission can diverge.**

   `duc emit` embeds one broad prelude while `emit-modules` builds a runtime
   header plus module artifacts. Both modes need the same dependency and runtime
   feature analysis so one mode does not conceal missing includes in the other.

## Required Architecture

1. Keep a project-independent Dudu runtime surface. Split it into stable
   feature-owned headers where useful, such as core types, Result, collections,
   indexing/views, hosted I/O, and shader support.
2. Compute dependencies from structured AST and semantic type use, not emitted
   strings.
3. Put native and Dudu dependencies required by public declarations in the
   generated module `.hpp`.
4. Put body-only native and Dudu dependencies in the generated module `.cpp`.
5. Make every generated `.cpp` include its own `.hpp` first. Emit private
   declarations and implementation bodies after that include rather than
   recreating the public interface independently.
6. Keep native macros and preprocessor effects scoped to the smallest generated
   artifact that needs them. Do not place application headers in the shared
   runtime or its precompiled form.
7. Make generated-CMake builds precompile only the stable Dudu runtime surface.
   Changes to one application-native header must not rebuild unrelated Dudu
   modules or regenerate the runtime PCH.
8. Make self-contained emission compose the same feature and dependency plan
   into one file rather than using a separate blanket-import path.

## Implemented State

Implemented July 15, 2026:

- `dudu_runtime.hpp` contains Dudu runtime support and required standard
  headers, never application C or C++ imports.
- generated module sources include their own generated headers first
- structured public-surface analysis places signature and inline-body
  dependencies in module headers and body-only dependencies in module sources
- a `cpp(...)` escape in a header-owned body conservatively makes that module's
  native imports header dependencies; Dudu does not parse opaque C++ text to
  guess a narrower dependency
- runtime support and standard headers are feature-gated from one AST scan per
  emitted prelude
- generated-CMake projects precompile the stable runtime header
- self-contained and module emission share the same feature analysis

Regression fixtures cover minimal, Result, collection, inferred fixed-array,
indexing, print, assert, shader, source-only native, public native type, and
opaque inline-native cases. Clean generated-CMake builds pass for
`raymarch-dd`, `dudu-webserver`, `dudu-datascience`, and `duduplayground`.

## Validation Matrix

1. Two independent Dudu modules import different native headers. Each generated
   source sees only its own private dependency.
2. A public function exposes a native type. The generated module header is
   self-contained and compiles when included by a fresh C++ translation unit.
3. A native type used only inside a function body appears only in the generated
   source dependency set.
4. A Dudu type crosses a module boundary. The consumer receives the direct
   generated-header dependency without relying on `dudu_runtime.hpp`.
5. Two native headers define conflicting macros. Unrelated modules remain
   isolated, and a genuine same-module conflict produces a local diagnostic.
6. Every generated `.hpp` compiles alone, and every `.cpp` compiles by including
   its own `.hpp` first.
7. Changing a body-only native header rebuilds only the owning module and true
   dependents. It does not rewrite or invalidate `dudu_runtime.hpp`.
8. Minimal `i32` output does not include hosted I/O, containers, indexing, or
   their standard headers.
9. Result, collection, indexing, shader, hosted, and freestanding fixtures each
   receive exactly the runtime components they require.
10. Equivalent self-contained and module builds compile and run with the same
    observable behavior.

## Completion Gate

- `dudu_runtime.hpp` contains no application C or C++ includes.
- Generated module sources include their own generated headers first.
- Public and private dependency placement is semantic and covered by fixtures.
- No test relies on accidental transitive native includes.
- Runtime PCH invalidation is independent of ordinary project-native imports.
- Cold, one-module edit, native-header edit, and no-op timings are recorded
  before and after the migration.

The compiler comparison after feature gating reduced the 1,000-unit external
C++ backend median from 487.7 ms and 156.6 MiB to 256.4 ms and 90.7 MiB. The
complete self-contained path fell from 716.2 ms to 477.4 ms. Incremental project
measurements remain tracked separately in
[Compiler Performance Matrix](compiler-performance-matrix.md).
