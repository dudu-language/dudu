# Le Plan

This is the next practical roadmap for Dudu.

Dudu is far enough along to guide development by making real examples easier
instead of planning the whole language in the abstract.

## Engineering Bar

Dudu has moved past throwaway prototype mode. Treat it as a real systems
language project.

Work on this plan should avoid temporary syntax, name-specific imported-library
hacks, string-lowering shortcuts for new language features, and hidden behavior
that cannot be explained as normal Dudu semantics. When a feature needs compiler
architecture first, build the architecture instead of adding a narrow workaround.

The standard is:

- real AST-backed language features
- readable generated C++
- diagnostics that point at Dudu source
- no imported library special cases
- no wrapper-header dependency for normal C/C++ interop
- examples that compile and run
- tests that prove behavior without trapping the dev loop in slow validation
- LSP/editor support that follows the same compiler model

If a planned feature cannot meet that bar yet, document the missing prerequisite
and implement the prerequisite first.

## Critical Module Import Blocker

Dudu-native imports must be fixed before the language can be considered sound
for normal multi-file programs.

Required behavior:

- importing the same physical Dudu module through multiple routes must create
  one canonical module identity, not duplicate declarations
- transitive imports must not leak into the importing module unless a facade
  module intentionally reexports them
- `import module.path`, `import module.path as alias`,
  `from module.path import Name`, and `from module.path import Name as Alias`
  must all work according to the documented Python-shaped import model
- direct-name collisions from selective imports must produce clear diagnostics
  with source locations and an aliasing suggestion
- cycles must be diagnosed at the module graph level, not through duplicate
  declaration fallout
- codegen should mirror module identity with C++ namespaces or another stable
  internal scheme so two import paths never emit two copies of the same Dudu
  declaration

Observed failure from the `raymarch-dd` sanity repo: importing `Vec3` directly
from `vec3` while also reaching it transitively through `camera` produced a
duplicate `Vec3` declaration. That is a fatal module-system bug, not acceptable
user friction. The fix belongs in the module loader/symbol table/codegen
architecture, not in examples that avoid normal import shapes.

Status: the module loader now canonicalizes physical `.dd` files and appends
each loaded module once in dependency-first order, so the same source reached
through multiple import routes no longer duplicates declarations. `from ...
import Name as Alias` also materializes Dudu-native aliases for types,
constants, and functions. Stricter Python-shaped namespace boundaries and
intentional re-export semantics remain unfinished; the current backend still
uses a merged module model rather than per-module symbol namespaces. Qualified
Dudu module imports such as `import camera as cam` and
`import renderer.camera` now bind `cam.Type`, `cam.function`,
`renderer.camera.Type`, and `renderer.camera.function` through non-emitted
module metadata. Distinct modules that declare the same unqualified Dudu type
or function name now preserve their declaration origin in the AST, which gives
the namespace backend the information it needs. Source-tree loads also preserve
the ordered per-file module units alongside the compatibility merged view, so
the next sema/codegen work can operate module-by-module. Same-name declarations
still require real per-module C++ namespaces before they can coexist through
semantic analysis and codegen.

## Feature Validation Bar

Every major language feature should land with both small and realistic Dudu
programs:

- parser/codegen shape fixtures for the simplest form
- negative fixtures for the obvious mistakes and diagnostics
- executable examples for normal use
- at least one real-ish stress example that combines the feature with another
  subsystem or imported C/C++ library when that is the reason the feature
  exists
- fast tests by default, with heavyweight or optional native-library probes kept
  separate from the normal dev loop

Examples of the expected bar:

- generics should cover `clamp[T]`, `Box[T]`, `Vec2[T]`, containers, callbacks,
  and imported C++ algorithm interop
- arrays should cover `array[T][N]`, `array[T][M, N]`, image/matrix kernels,
  inferred literal shapes, swizzling, slicing views, C pointer/span handoff,
  and library tensor indexing hooks
- sum types should cover lexer tokens, UI/game events, network messages,
  recursive expression trees, and exhaustive-match diagnostics
- inheritance should cover simple bases, abstract contracts, partial abstract
  bases, multiple interface-like bases, `super`, imported C++ bases, and an
  autograd-style graph
- macro prerequisites should include serde-like derives, binary serialization,
  reflection metadata, generated tests, and export-table generation as target
  examples

## 1. Finish The Project Driver

Primary plan: [Dudu Project Driver Plan](project-driver-plan.md).

Make daily use feel good:

- named targets: done
- `dudu clean`: done
- clean Cargo-ish build logs: done
- `dudu test ./...`: done
- better test binary output paths: done
- examples and docs that prefer `dudu run` where it improves usability: done

This pays off immediately because every future example and feature becomes
easier to build, run, and test.

## 2. Constructors, Destructors, And Operators

Primary plan: [OOP Plan](oop-plan.md).

Related specs:

- [Appearance Spec](appearance-spec.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Make the C++ object model feel complete through Python-shaped syntax:

- constructor behavior
- destructor behavior
- overloads
- operator overloads
- member methods
- class-scoped functions
- explicit static fields
- C++ interop behavior for all of the above

Status: constructors, destructors, member methods, imported C++ operator
overloads, Dudu-native operator methods, imported C++ base-method lookup,
`init`/`drop`, class-scoped functions without `@staticmethod`, explicit
`static[T]` fields, and `@operator(...)` are implemented. Broader overload-set
polish remains part of header-awareness hardening.

This is more important than user-defined macros because it directly affects
normal systems and game code.

## 3. Static Members And Namespaced Constants

Related specs:

- [Appearance Spec](appearance-spec.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Support class-scoped constants, static data, and static functions.

Status: class-scoped `ALL_CAPS` constants are static constants,
`name: static[T] = value` is mutable class-shared state, and functions inside a
class with no `self` are class-scoped functions. Broader module and namespace
constants remain a candidate if real examples need cleaner organization than
file-level constants.

Python, Rust, C, and C++ all have ways to hang values off a type or namespace.
Dudu needs that so constants and helpers do not all live in the global module
scope.

Target shape:

```python
class Color:
    WHITE: Color = Color(1.0, 1.0, 1.0, 1.0)

    r: f32
    g: f32
    b: f32
    a: f32

class Math:
    PI: f64 = 3.141592653589793

    def clamp(x: f32, lo: f32, hi: f32) -> f32:
        if x < lo:
            return lo
        if x > hi:
            return hi
        return x
```

## 4. Harden Native Header Awareness

Primary plan: [Native Header Awareness Plan](header-awareness-plan.md).

The C/C++ interop promise depends on imported headers feeling reliable.

Drive this with real library stress tests:

- raylib
- SDL3
- ImGui
- glm
- sqlite
- POSIX
- C++ standard library containers and utilities

Important areas:

- macros
- templates
- overloads
- namespaces
- inherited C++ classes
- diagnostics when Clang tooling or include paths are wrong

## 5. Polish The Test System

Primary plan: [Dudu Tests](tests.md).

Keep it Cargo-ish.

Short-term fixes:

- `0 tests` output instead of `0/0 tests passed`: done
- unique test binary paths: done
- `dudu test ./...`: done
- `assert expr, "message"`: done

Longer-term features:

- `@test.ignore`: done
- `@test.should_panic`: done
- stdout capture: done
- no-capture mode: done

## 6. Keep Macros And Decorators Conservative

Related specs:

- [Appearance Spec](appearance-spec.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Do not build a full user-defined macro system yet.

Do add compiler-recognized decorators when they remove real friction:

- `@test`
- `@inline`
- `@extern_c`
- `@operator`
- target attributes such as `@cuda.global`

User-defined decorators, hygienic macros, and lisp-style compile-time
metaprogramming are separate language design work and should not block the
core C/C++ replacement layer.

## 7. Add Native Inheritance Deliberately

Related docs:

- [Inheritance Plan](inheritance-plan.md)
- [Native Header Awareness Plan](header-awareness-plan.md)
- [Python Subset Compiler Plan](python-subset-compiler-plan.md)

Dudu-native inheritance is now planned, but it should be implemented against the
real AST/type model rather than patched onto string lowering.

For C++ interop, Dudu still needs to consume inherited C++ classes well enough
to:

- call inherited methods: initial imported C++ support is implemented
- construct C++ types that use inheritance: initial imported C++ support is implemented
- pass derived/base pointers and references correctly: initial imported C++ support is implemented

Native inheritance should follow the inheritance plan: method-only `@abstract`,
`@override`, `@virtual`, `super`, strict multiple inheritance, interface-like
abstract classes, and C++-faithful diagnostics.

## Remaining Completion Checklist

These are the remaining practical completion areas for the current language
push. They are not release packaging work.

1. Guard The Development Loop

   Keep validation fast before starting broad compiler rewrites. Split or guard
   slow fixtures, keep optional native-library probes optional, and avoid
   repeatedly running heavyweight tests while iterating on parser/sema changes.

   Status: the `std_vector_map_string` codegen-shape hang is fixed and the fast
   suite is reliable again. Keep new validation targeted and guarded so one slow
   fixture does not stall the development loop.

2. Real AST Architecture

   Primary plan: [AST Plan](ast-plan.md).

   Move function bodies from raw statement strings to real statement,
   expression, and type AST nodes. This unlocks better errors, fix-its, semantic
   highlighting, LSP quality, native Dudu generics, and structured macros.

   Status: normal template-call emission lowers bracket arguments from parsed
   `TypeRef` nodes, including non-type value arguments, instead of falling back
   to raw expression text. Parsed method calls on pointer-typed member
   receivers such as `self.left.backward(...)` lower through member-path type
   information instead of raw pointer-member rewriting. Semantic token
   generation now merges native header metadata as a classification layer, so
   native C/C++ references in Dudu source can carry the LSP `native` modifier
   while token ranges remain anchored to the open Dudu file. Member-path type
   checks for normal local value paths now walk parsed expression nodes for
   nested `Name`/`Member`/`Index` shapes, and member C++ emission uses receiver
   type information for static-object-then-instance-field access. Nested
   indexed member assignment targets use the same parsed expression path before
   compatibility fallbacks. Generic method calls on nested receivers also
   type-check through parsed callee receiver expressions. Pointer receiver
   emission now uses parsed member expression typing instead of reconstructing
   a member path string first. Nested expression emission preserves symbol
   context through callee, member, dict-entry, named-argument, index,
   collection literal, tuple, template-call argument, swizzle, pointer-cast,
   and fixed-array literal children. Enum variant recognition now uses a shared
   structural expression helper in sema, codegen, and match patterns instead of
   duplicate dotted-string reconstruction. Native import member type lookup now
   walks parsed member expressions inside `sema_native` before crossing into
   native metadata table spelling. `class.name` static access in expression and
   assignment sema now carries the current class through parsed member
   expressions instead of normalizing reconstructed member-path strings.
   Ordinary and templated call sema now resolve `class.method(...)` from parsed
   callee expressions, removing the old current-class string normalizer.
   `super.init(...)` recognition in sema and class emission now checks parsed
   member-callee shape instead of reconstructed callee text. Type
   compatibility exposes parsed `TypeRef`
   overloads, and annotated local initializers validate against the parsed
   expected type node when available. `TypeRef` assignment compatibility keeps
   `list`, `set`, `dict`, `Option`, and `Result` expected types on parsed
   template-child nodes for local initializer checks instead of rendering those
   shapes back to strings first. Type-to-type compatibility now structurally
   matches parsed pointer, reference, wrapper, template, and function type
   nodes before falling back to native spelling compatibility. Typed `for`
   loop binding checks now compare against the parsed binding `TypeRef` before
   using alias/native spelling fallback. Delete/free checks now classify
   pointer arguments through parsed `TypeRef` nodes instead of raw leading `*`
   spelling. Indexed type inference now requires parsed index expressions
   instead of accepting public string index text, including the explicit
   `cpp(...)` escape inference path.

3. OOP Surface Cleanup

   Primary plan: [OOP Plan](oop-plan.md).

   Align implementation with the documented surface: `init`, `drop`,
   `static[T]`, no `@staticmethod`, no properties, `@operator(...)`, and static
   access through `Type.name` or `class.name`.

   Status: explicit `public`/`private` visibility keywords are rejected for
   normal Dudu syntax. Function and method privacy follows Python naming:
   leading-underscore names stay out of generated public headers while normal
   names remain public. Out-of-line methods such as
   `def Player.damage(self, amount: i32):` attach to the declared class and use
   the normal method sema/emission path. `@staticmethod`, `@classmethod`, and
   `@property` are rejected with explicit OOP-surface diagnostics.

4. Arrays, Matrices, Tensors, And Slicing

   Primary plan: [Arrays, Matrix, Tensor, And Slicing Plan](arrays-indexing-plan.md).

   Migrate current fixed-array syntax to canonical `array[T][shape]`, add
   initializer-based shape inference for `array[T] = literal`, then add
   shape-aware fixed arrays, matrix/tensor indexing, swizzling, and explicit
   view/copy slicing semantics so numeric, graphics, and image code feels
   natural without hidden allocation.

   Status: explicit `array[T][N]` and `array[T][M, N]` compile through the
   string and TypeRef lowering paths, explicit-shaped literal initializers are
   checked, `array[T] = literal` infers fixed shapes, and comma indexing such as
   `mat[row, col]` lowers for Dudu-native fixed arrays. Partial array indexing
   is type-aware and over-indexing is diagnosed. Fixed arrays work with
   `len(values)` and `&values[0]` for native pointer/count handoff.
   One-dimensional fixed-array `start:end`, `:end`, `start:`, and `:` slices
   produce `span[T]` views. Fixed multidimensional arrays support trailing row
   views such as `mat[row, :]`. Matrix column slices such as `mat[:, col]`
   produce `strided_span[T]` views so non-contiguous views are explicit.
   Dudu-native `@operator("[]")` read hooks and `@operator("[]=")` indexed
   assignment hooks work for library-style tensor wrappers, and indexed member
   paths such as `self.values[i]` type-check. One-dimensional fixed-array step
   slices such as `values[start:end:step]` produce `strided_span[T]` views.
   General multidimensional slices and richer tensor-library metadata remain.
   Same-width Dudu-native `xyzw`, `rgba`, and `stpq` read swizzles are
   implemented for local class receivers and expression receivers. Same-width
   Dudu-native write swizzles are implemented for assignable receivers and
   reject repeated write components. Different-width local Dudu-native read
   swizzles construct matching result classes when available, such as
   `Vec4.xy -> Vec2`. Imported vector swizzles use scanned field metadata for
   read and assignment, including different-width results when a matching
   imported vector class exists.

5. Native Dudu Generics

   Primary plan: [Generics Plan](generics-plan.md).

   Add compile-time generic functions and classes such as `def clamp[T](...)`
   and `class Vec2[T]`, lowering to readable C++ templates and producing
   Dudu-source diagnostics for instantiated errors.

   Status: generic parameter syntax for Dudu-native classes and functions
   parses into the AST, type parameters are visible while checking declarations
   and bodies, duplicate generic parameters are diagnosed, and generic
   classes/functions emit readable C++ templates. Explicit generic class
   construction works for simple value-type examples such as `Box[i32]`, and
   constructor arguments are checked against instantiated fields or `init`
   signatures. Explicit generic free-function calls such as
   `identity[i32](42)` instantiate Dudu signatures from parsed template-call
   nodes and type-check parsed runtime arguments. Simple free-function
   call-site inference works when every type parameter can be bound from
   argument types, including nested forms such as `list[T]`. Explicit generic
   methods such as `box.id[i32](42)` and
   `box.choose[str, i32](name, value)` emit C++ method templates, substitute
   method and class type parameters positionally, and type-check parsed runtime
   arguments. Generic method type-argument arity is diagnosed in Dudu source.
   Generic bodies allow operators where both operands are the same visible
   generic type parameter, which supports target examples such as `Vec2[T]`.
   Dudu-native generic instantiation substitutes parsed type nodes for nested
   templates, fixed arrays, wrappers, and function types. Generic method type
   arguments can also be inferred from typed return/assignment context for
   calls such as `value: i32 = box.make()` and `return box.make()` from an
   `i32` function. Non-type parameters and richer instantiated diagnostics
   remain.
   Multi-parameter generic functions and classes such as `Pair[str, i32]`
   substitute receiver member types through the declared class generic
   parameter names. Target fixtures now cover `Stack[T]`, generic
   `Arena[T]`/`Handle[T]` storage, `Result[T, E]` helpers, a generic
   `sort_by[T]` wrapper over `std.sort`, generic span math with `T()`
   default construction,
   argument-inferred generic method calls such as `box.id(42)`, and
   return-context inferred method calls such as `box.make()`.

6. Sum Types And Pattern Matching

   Primary plan: [Sum Types And Pattern Matching Plan](sum-types-plan.md).

   Extend simple enums into safe Rust-style tagged unions with payload variants
   and exhaustive `match`. This gives Dudu static heterogeneous data without
   `Any` or silent dynamic containers.

   Status: simple zero-payload enum `match` statements parse, type-check,
   enforce exhaustiveness, reject duplicate or unknown cases, and lower to
   readable C++ `switch`. Payload variants parse, validate, lower to tagged
   `std::variant` wrappers, construct through `Enum.Variant(...)`, and support
   exhaustive `match` with positional and named payload bindings. Match guards
   are implemented and type-checked as `bool`; guarded cases do not count
   toward exhaustiveness. `Option[T]` and `Result[T, E]` wrapper matching is
   implemented with exhaustive `Some`/`None` and `Ok`/`Err` cases. Recursive
   enum examples using pointer indirection compile and run. A lexer-token
   fixture covers tuple payloads, named payloads, guarded payload cases, named
   destructuring, and exhaustive matching. An event/message fixture covers
   UI, command, and network-style payload enums, including enum payload fields
   that carry Dudu-native classes declared in the same module. Anonymous
   `variant[...]` parses, validates alternatives, lowers to `std::variant`,
   and supports assignment from exactly one matching alternative, which covers
   explicit mixed containers such as `list[variant[i32, str]]`. Unreachable
   diagnostics reject duplicate unguarded cases, wildcard cases after
   exhaustive coverage, and later cases after an unguarded wildcard. Broader
   pattern-subsumption diagnostics remain.

7. Native Inheritance

   Primary plan: [Inheritance Plan](inheritance-plan.md).

   Implement method-only `@abstract`, `@virtual`, `@override`, `super`, current
   class static access, strict multiple inheritance, and interface-like abstract
   classes. Keep imported C++ inheritance behavior generic and header-driven.

   Status: native Dudu base lists parse and validate, generated C++ emits
   public inheritance with base classes ordered before derived classes, and
   inherited fields/methods are visible to member access. Method-level
   `@abstract`, `@virtual`, and `@override` are implemented with override
   target/signature diagnostics and pure-virtual lowering. `super.method(...)`
   is implemented for single-base classes and lowers from the parsed call AST
   to explicit base-method dispatch. Construction and `new[T]` allocation of
   Dudu classes with unimplemented abstract methods are rejected with Dudu
   diagnostics. `super.init(...)` is implemented for single-base constructors
   as the first statement in `init`, validates parsed arguments against the
   base constructor, and lowers to a C++ base initializer. Broader constructors
   across multiple storage bases remain rejected. Strict native multiple
   inheritance rules are implemented for the common one-storage-base plus
   interface-like abstract bases shape, including duplicate concrete-method
   diagnostics. Classes with virtual or abstract instance methods, and classes
   deriving from them, now emit virtual destructors automatically; `drop`
   lowers to a virtual destructor in those classes. An autograd-style graph
   fixture covers abstract base nodes, derived operation nodes, derived-to-base
   raw pointers, virtual dispatch through base pointers, and method calls
   through pointer-typed member fields. Generic Dudu-native bases such as
   `Repository[Player]` substitute type parameters during override checking.

8. Macro Surface Prerequisites

   Primary plan: [Macro Syntax Plan](macro-syntax-plan.md).

   Start with target syntax for derives, field attributes, serde-like codegen,
   tests, reflection metadata, binary serialization, and binding generation.
   Prefer AST-backed declaration macros over raw string macros. Decorators now
   preserve parsed expression nodes alongside their original text, and
   compiler-recognized decorators use a shared parsed-expression helper for
   name matching and first-argument extraction. Decorator parsing has regression
   coverage for string arguments containing operator characters, such as
   `@operator("+")`.

9. Native Header Hardening

   Improve overload diagnostics, const/reference modeling, explicit C++
   template calls, template-heavy library behavior, header cache invalidation,
   cache cleanup, and scanner failure UX.

   Status: overload diagnostics, explicit native C++ template function calls
   including multi-argument calls, local-header cache invalidation, cache
   cleanup, broken-Clang diagnostics, missing-header diagnostics, and common
   const pointer/reference lowering are implemented. Explicit native template
   allowance is limited to explicit template-call syntax on imported native
   prefixes whose scanner metadata is incomplete. Aliased native imports avoid
   double-prefixing names that already come from the imported namespace, native
   overload matching infers common C++ template placeholders and variadic packs,
   preserves scanned C++ template parameter order for explicit template calls,
   handles nested `<...>` parameter splitting, preserves native enum constant
   types, normalizes C tag spellings for native field lookup, and treats
   internal C++ implementation template aliases as opaque compiler artifacts.
   The standard-library algorithms fixture now validates representative
   containers, algorithms, pairs, tuples, and `std.get` without wrapper headers.
   Native overload failure diagnostics list argument types, candidate
   signatures, and per-candidate arity or first-mismatched-parameter reasons.
   Broader template-heavy library behavior remains the main hardening area.

10. Real Library Stress Tests

   Keep proving SDL3, ImGui, raylib, glm, sqlite, POSIX, OpenCL, Vulkan, GLFW,
   and FFmpeg style APIs with normal imports and minimal wrapper code.

   Current optional probes pass for glm, OpenCV, sqlite, threading, POSIX mmap,
   POSIX pthread, raylib, SDL3, GLFW, OpenCL, Vulkan, and FFmpeg on this
   machine. Optional dev-only dependencies can be installed into the ignored
   `third_party/install` prefix with `scripts/setup_dev_deps.sh`; the main Dudu
   build does not require them.

11. Incremental Build Strategy

   Move beyond generated-one-file builds where needed. Generate C++ per Dudu
   module so CMake/Ninja can rebuild changed translation units instead of whole
   programs.

   Status: direct native builds preserve generated C++ mtimes and skip the
   C++ compile/link step when the generated C++, native build command, and
   native source inputs are unchanged. True per-module generated C++ remains
   the larger architecture step and is a major requirement for a serious C/C++
   ecosystem-facing toolchain. Current `duc emit` and direct `dudu build`
   output one generated C++ translation unit for the whole Dudu source tree;
   source-tree module units are now preserved in the AST so that future
   per-module `.hpp/.cpp` output has authoritative module boundaries to emit
   from. The emitter can now produce inspection artifacts for each module unit,
   but the direct native build still compiles the compatibility single-file
   output until namespace-aware per-module sema/codegen is complete.

12. Language Server And Formatter

   Implement `duc lsp` so editors can show diagnostics, warnings, hover,
   completion, go to definition, find references, rename, formatting, and native
   header navigation. The concrete plan is
   [Language Server Plan](language-server-plan.md).

   Build LSP and formatter behavior from the same AST/sema model used by the
   compiler.

13. Project Driver Polish

   Keep using `dudu` on real projects and fix friction in native build inputs,
   target selection, diagnostics, generated build files, and examples.

   `dudu build`, `dudu run`, and `dudu test` are the serious user-facing
   commands. Backend choice is an implementation detail. The direct backend is
   real, fast, and intentionally narrow. The CMake backend is the broad native
   ecosystem backend for CMake package discovery, IDE/project generators, and
   larger native dependency graphs. `dudu cmake` remains an inspectable
   artifact/debug/handoff command, not the primary serious-project workflow.
   Backends must fail clearly when they cannot model a project rather than
   silently dropping C/C++ build-system details.

   The implementation target is not "simple apps use `dudu build`, real apps
   leave for CMake." The target is "real apps still use `dudu build`, and Dudu
   chooses or is told which native backend to drive."
   CMake is a first-class native ecosystem backend, not a fallback that users
   must escape into when `dudu build` stops being serious.

   This is part of the language's C/C++ interop promise. Dudu should cooperate
   with CMake, pkg-config, native compiler flags, vendored C/C++ sources, and
   user-owned native build files without turning `dudu build` into a toy-only
   command. Direct compilation is the small fast backend. CMake is the broad
   native ecosystem backend. `dudu cmake` is the inspectable artifact and
   native handoff path, not the replacement for the normal command surface.

   This is not a Zig-style attempt to make the language build system own every
   native project. Zig's normal model is a `build.zig` graph that owns the
   build and can compile/link C and C++ inputs through that graph. Zig build
   scripts can run external tools when users write that integration, but Zig
   does not have a built-in "revert to CMake" backend. Dudu's native interop
   goal is broader in a different direction: direct compiler builds, generated
   CMake builds, and user-owned CMake builds are backend modes behind the same
   command surface, not separate classes of Dudu project. CMake-backed builds
   should still be launched through `dudu build`, `dudu run`, and `dudu test`.

   Dudu is not copying Zig here. CMake is not a shameful fallback after
   `dudu build` gives up. It is one of the serious native backends because
   CMake is where a large part of the C/C++ ecosystem already lives.

   Status: `dudu build`, `dudu run`, and `dudu test` use the direct backend by
   default. `[build] backend = "direct"` and `[build] backend = "cmake"` parse
   from the manifest. The direct backend is selectable explicitly. The
   generated CMake backend is implemented for `dudu build`, `dudu run`, and
   `dudu test`; it emits an internal CMake project and drives `cmake -S/-B`
   plus `cmake --build`. Native inputs such as include paths, library paths,
   libraries, flags, pkg-config packages, and extra C/C++ sources are partially
   implemented and useful. `dudu cmake` still emits CMake for inspection or
   handoff. User-owned CMake projects are not implemented as a backend mode.
   The serious path is to make those backend choices work behind the same
   front-door commands, not to tell users that real projects must leave
   `dudu build`.

   Required backend milestones: explicit backend selection in `dudu.toml`,
   generated-CMake builds behind `dudu build`, `dudu run`, and `dudu test`,
   user-owned CMake projects as a backend mode, and diagnostics that reject a
   backend when it cannot honestly model the manifest.

   Path handling is part of the project-driver contract. Manifest entries such
   as `entry`, `build.dir`, include paths, library paths, and native source
   lists are relative to the directory containing `dudu.toml`. Explicit local
   imports remain source-file relative. Invoking `dudu` from a different working
   directory must not change behavior when it finds the same manifest.

14. Freestanding And Embedded Assert Policy

   Hosted `assert` and `debug_assert` are implemented. Freestanding and
   embedded targets reject runtime `assert` instead of accidentally emitting
   hosted runtime machinery.

15. Macro Edge Cases

   Normal imported macros are covered. Keep token-pasting, declaration-
   generating, and partial-syntax macros behind wrapper headers unless a real
   library forces a better design.

16. Remove Prototype Cruft

   Before calling this language push complete, scan the codebase and docs for
   prototype-era compatibility paths, internal alternate spellings, and legacy
   conveniences that do not match the current language design. Dudu has no
   compatibility promise to existing users, so stale paths should be removed
   instead of preserved.

   Specific examples to audit include old dunder-style operator spellings,
   wrapper workarounds that were replaced by header awareness, and any hidden
   string-lowering shortcuts that bypass the real AST/sema model. Explicit
   user escape hatches such as `cpp(...)` can remain, but compiler-internal raw
   string fallbacks should disappear as AST coverage reaches the corresponding
   language forms. Prototype Python sugar such as `lambda`, ternary conditional
   expressions, comprehensions, generator expressions, and RHS `def`
   expressions should be rejected or removed from examples in favor of
   statement-only named `def` declarations and explicit statements. Function
   names remain values after declaration, so callback tables and sort
   predicates should be written with named declarations rather than inline
   function literals.
