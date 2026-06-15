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
   while token ranges remain anchored to the open Dudu file.

3. OOP Surface Cleanup

   Primary plan: [OOP Plan](oop-plan.md).

   Align implementation with the documented surface: `init`, `drop`,
   `static[T]`, no `@staticmethod`, no properties, `@operator(...)`, and static
   access through `Type.name` or `class.name`.

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
   views such as `mat[row, :]`.
   Dudu-native `@operator("[]")` read hooks and `@operator("[]=")` indexed
   assignment hooks work for library-style tensor wrappers, and indexed member
   paths such as `self.values[i]` type-check. Step slices, column/general
   multidimensional slices, different-width swizzle result construction,
   and imported vector swizzle metadata remain. Same-width Dudu-native `xyzw`,
   `rgba`, and `stpq` read swizzles are implemented for local class receivers
   and expression receivers. Same-width Dudu-native write swizzles are
   implemented for assignable receivers and reject repeated write components.

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
   templates, fixed arrays, wrappers, and function types. Non-type parameters,
   inferred method type arguments, and richer instantiated diagnostics remain.
   Multi-parameter generic functions and classes such as `Pair[str, i32]`
   substitute receiver member types through the declared class generic
   parameter names. Target fixtures now cover `Stack[T]`, `Result[T, E]`
   helpers, and generic span math.

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
   destructuring, and exhaustive matching. Anonymous `variant[...]` remains.

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
   through pointer-typed member fields.

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
   and common libstdc++ dependent helper types normalize to the underlying Dudu
   type. The standard-library algorithms fixture now validates representative
   containers, algorithms, pairs, tuples, and `std.get` without wrapper headers.
   Deeper overload diagnostics and broader template-heavy library behavior
   remain the main hardening areas.

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
   ecosystem-facing toolchain.

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
   artifact/debug/escape command, not the primary serious-project workflow.
   Backends must fail clearly when they cannot model a project rather than
   silently dropping C/C++ build-system details.

   The implementation target is not "simple apps use `dudu build`, real apps
   leave for CMake." The target is "real apps still use `dudu build`, and Dudu
   chooses or is told which native backend to drive."

   Status: `dudu build` and `dudu run` currently use the direct backend.
   Native inputs such as include paths, library paths, libraries, flags,
   pkg-config packages, and extra C/C++ sources are partially implemented and
   useful. `dudu cmake` emits CMake, but the CMake backend for `dudu build`,
   `dudu run`, and `dudu test` is not implemented yet. User-owned CMake
   projects are not implemented as a backend mode yet. The serious path is to
   make those backend choices work behind the same front-door commands, not to
   tell users that real projects must leave `dudu build`.

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
