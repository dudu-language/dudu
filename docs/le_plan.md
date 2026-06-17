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
- a conventional compiler pipeline with clear phase ownership: parser builds
  AST, module resolver builds canonical module identities and imports, name
  resolver binds symbols, type checker annotates or records semantic facts,
  lowering produces an explicit backend-facing representation, and C++/CMake
  emission consumes that representation
- readable generated C++
- diagnostics that point at Dudu source
- no imported library special cases
- no wrapper-header dependency for normal C/C++ interop
- examples that compile and run
- tests that prove behavior without trapping the dev loop in slow validation
- LSP/editor support that follows the same compiler model

If a planned feature cannot meet that bar yet, document the missing prerequisite
and implement the prerequisite first.

The current implementation grew from a prototype, so some compatibility strings
and backend shortcuts still exist. The goal is not to freeze that shape. The
goal is to keep working features moving while steadily separating the compiler
into boring, traceable phases. New work should push responsibilities into the
proper phase instead of adding more cross-phase patching.

## Proper AST And Compiler Pipeline

This is the next architecture priority. Dudu must stop treating raw statement
text as semantic data.

The clean compiler split is:

1. Lexer/tokenizer

   Turn source into tokens with exact ranges. Keep comments and trivia for the
   formatter, LSP, doc comments, and diagnostics.

2. Parser to sugared AST

   Build a source-shaped AST with real statement, expression, and type nodes:
   `IfStmt`, `ForStmt`, `VarDeclStmt`, `AssignStmt`, `ReturnStmt`,
   `BinaryExpr`, `CallExpr`, `MemberExpr`, `IndexExpr`, and structured
   `TypeRef` nodes. Every relevant node carries a source range.

3. Lowering/desugaring

   Convert convenient Python-shaped syntax into a smaller core AST or IR where
   useful: augmented assignment, range loops, destructuring, operator sugar, and
   other explicit language conveniences.

4. Semantic analysis

   Run name binding, overload resolution, type checking, control-flow facts,
   and module export checks on structured AST/core AST nodes, not strings.

5. Codegen

   Emit C++ from structured nodes and semantic facts. Codegen must not
   reconstruct Dudu syntax by joining source strings and parsing them again.

6. Diagnostics, lint, LSP, and formatter

   Consume the AST, token/trivia model, symbol tables, and semantic facts.
   Hover, definition, references, rename, diagnostics, quick fixes, semantic
   tokens, and formatting all follow the same compiler model.

Raw source text is still valid compiler data for comments, doc comments,
literal spelling, exact diagnostic snippets, formatter trivia, and explicit
escape hatches such as `cpp(...)`. Raw text is not valid as the source of truth
for Dudu syntax after parsing.

Migration path:

- add proper token stream and parser APIs for statement forms
- make block parsing produce structured `Stmt` nodes directly
- keep legacy string fields only as temporary compatibility mirrors while
  callers move over
- migrate sema, codegen, lints, diagnostics, LSP, and formatter form by form to
  structured nodes
- delete `statement_from_text`, substring-based statement classification, and
  compatibility string fields once callers no longer need them
- add regression tests proving comments, strings, and formatting trivia do not
  affect semantics, lints, test discovery, references, or rename

This work also sets up separate-file compilation. Per-module generated C++
needs the compiler to understand modules as real units with stable public
interfaces:

- parse each `.dd` file into its own AST module
- analyze and export only public declarations from that module
- generate one `.hpp/.cpp` pair or equivalent artifact per module
- emit imports as generated includes and references instead of flattening every
  module into one mega-file
- use stable generated names and namespaces per module
- track dependencies so CMake/Ninja rebuilds only changed generated files

The current merged-output model is a compatibility backend, not the long-term
shape of the compiler. A real AST, module graph, symbol/export model, and CMake
backend are the path to clean separate-file output.

## Diagnostic And Lint Cleanup

All diagnostics, lint warnings, code actions, hover data, definition lookup,
and fix-its must move onto parser AST, resolved symbols, typed expressions, and
semantic control-flow facts.

The current editor lint pass still contains prototype string/regex heuristics
for things like unreachable code, unused locals, shadowing, suspicious casts,
and raw `cpp(...)` warnings. That is not acceptable as the final architecture.
These checks can produce false positives because they do not understand real
statement trees, expression continuations, imports, scopes, overloads, or typed
control flow.

Required behavior:

- parser errors come from parser state and AST construction, not line-text
  guesses
- semantic errors come from name/type/control-flow analysis over AST nodes
- lint warnings use the same AST and semantic model as the compiler
- LSP code actions are tied to concrete AST ranges and edits
- hover, go-to-definition, references, and rename use resolved symbols rather
  than reconstructed strings
- string/regex lint heuristics must either be deleted or replaced with
  AST-backed implementations
- tests must include realistic false-positive cases, not only toy positive
  lint fixtures

Until this is complete, prefer no lint diagnostic over a noisy wrong lint
diagnostic. Compiler correctness diagnostics are more important than clever
editor warnings.

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
now also carry stable generated C++ names derived from their owning module, but
the direct backend and semantic lookup still need to switch to those names
coherently before they can coexist through normal semantic analysis and
codegen. The per-module artifact emitter now uses those generated names for
same-module declarations and qualified imported module references, including
`import module as alias`, `import module.path`, and selective `from module
import Name` forms. `duc emit-modules` now analyzes each module unit in its
own semantic scope, and per-unit imports materialize qualified/selective
symbols without pulling dependency declarations into the current module. The
generated CMake backend has a regression fixture with two modules that both
declare `Box`, `make`, and `score`; those compile as separate generated C++
artifacts with distinct generated names. A CMake-backend negative fixture now
rejects transitive import leakage, so importing a facade module does not let the
importer use the facade's private dependencies by accident. The direct backend
still uses the compatibility merged output and remains intentionally narrower
until semantic lookup and direct codegen fully move to module namespaces, but it
now rejects cross-module declaration-name collisions with a backend-aware
diagnostic that points users at `[build] backend = "cmake"` or
`duc emit-modules` instead of surfacing generic duplicate-declaration fallout.

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
   suite is reliable again. The dense `cpp_stdlib_algorithms` scanner fixture is
   intentionally outside the default broad script and lives in
   `scripts/probe_cpp_stdlib_algorithms.sh` with a timeout, because system STL
   header scanning is useful coverage but too slow for every dev-loop pass.
   Keep new validation targeted and guarded so one slow fixture does not stall
   the development loop.

2. Real AST Architecture

   Primary plan: [AST Plan](ast-plan.md).

   Move function bodies from raw statement strings to real statement,
   expression, and type AST nodes. This unlocks better errors, fix-its, semantic
   highlighting, LSP quality, native Dudu generics, and structured macros.

   Status: block parsing now routes keyword/control statements through a
   token-directed statement parser instead of classifying those statements from
   raw line text. `return`, `if`, `elif`, `else`, `match`, `case`, `while`,
   `for`, `try`, `except`, `raise`, `delete`, `assert`, `debug_assert`,
   `cpp(...)`, `pass`, `break`, `continue`, variable declarations,
   assignments, compound assignments, bare expression statements, and
   unsupported local-only Python statements now get their statement kind and
   parsed fields from parser state. Normal function block parsing no longer
   calls `statement_from_text`, and the legacy compatibility parser has been
   removed. Statement mirror fields for parsed targets, iterables, match
   patterns, guards, conditions, assert messages, and general values have also
   been removed; downstream code now uses the structured expression fields, with
   `source_text` retained only as source/trivia and explicit `cpp(...)` escape
   bodies stored separately. The raw `Stmt::text` field has been removed.
   Import declarations now also carry whole-statement source ranges/trivia, and
   LSP organize-import/missing-import actions use parsed import declarations for
   import identity and block placement rather than prefix-scanning import lines.
   Expression parsing now enters through the lexer/token stream instead of the
   old top-level substring classifier, preserving structured AST nodes for
   calls, template calls, indexes, slices, literals, unsupported lambdas/def
   expressions, and Python conditional expressions. The token expression parser
   has been split into a small public entry point plus focused core and
   postfix/primary parser files. Type parsing now follows the same shape:
   `parse_type_text` and `parse_type_list` lex their input and build `TypeRef`
   nodes through a token parser instead of string-scanning top-level arrows and
   brackets. Existing type node shapes are preserved, including pointers,
   references, wrappers, templates, fixed arrays, function pointer types,
   non-type template value arguments, and C tag spellings such as
   `struct stat`.
   Normal template-call emission lowers bracket arguments from parsed
   `TypeRef` nodes, including non-type value arguments, instead of falling back
   to raw expression text. Template-call C++ emission now synthesizes
   compatibility text for temporary `TypeRef` nodes from the parsed type shape
   instead of copying the template-call expression span. Parsed method calls
   on pointer-typed member receivers such as `self.left.backward(...)` lower
   through member-path type information instead of raw pointer-member rewriting.
   Semantic token
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
   callee expressions, removing the old current-class string normalizer, and
   normal/static member call sema no longer falls back to splitting
   reconstructed dotted callee strings for Dudu-owned calls. Reconstructed
   dotted callees remain only at native import prefix boundaries.
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
   `cpp(...)` escape inference path. Fixed-array index and slice metadata now
   reads parsed `TypeRef` shape/element nodes in the remaining compatibility
   index helpers before crossing native/operator fallback boundaries. Emitted
   local index type inference now tries parsed `TypeRef` handling before
   falling back to native-spelling string template extraction. Template call
   sema and codegen now consume parser-produced `template_type_args` directly
   and reject malformed internal template-call ASTs instead of reparsing
   expression argument text as types. Expression template-call parsing now
   builds expression and type template arguments from existing token spans for
   direct bracket calls and templated pointer casts instead of substring
   reparsing inside the token parser. Parser declaration and statement code now
   builds expression and type pieces through token spans directly, and the
   parser layer no longer calls the top-level text expression/type parsers as a
   compatibility fallback. Pointer type C++ emission helpers and pointer-cast
   call emission now wrap parsed pointee types in explicit `TypeRef::Pointer`
   nodes instead of concatenating `*` onto type text and reparsing it.
   Templated pointer-cast semantic inference also builds the pointee from
   parsed template `TypeRef` arguments instead of reconstructing `Name[...]`
   text and reparsing it. Inferred array literal shapes now carry a
   fixed-array `TypeRef`, so sema and C++ emission bind inferred array locals
   without regenerating `array[T][...]` text and reparsing it. Generic
   argument inference now keeps inferred bindings as `TypeRef` nodes, so
   function and method generic inference returns structured type arguments
   instead of reparsing rendered binding text. Generic function and method
   inference can also request typed expression results through
   `infer_expr_type_ast`, so local/type-aware arguments cross that boundary as
   `TypeRef` nodes before hitting compatibility fallback paths. Body semantic
   checking now exposes typed expression inference through
   `BodyCheckCallbacks`, so return-context generic method inference and
   statement-level `delete` checks can pass `TypeRef` values through body sema.
   Implicit local bindings now preserve inferred `TypeRef` metadata when body
   sema has typed expression results available.
   Deallocation argument checks now accept `TypeRef` nodes, so `delete` and
   `free` validation checks pointer shape structurally instead of parsing
   rendered argument type strings inside the allocator helper.
   Dereference assignment target checks now use typed expression inference and
   peel pointer `TypeRef` nodes instead of reparsing rendered pointee type
   strings.
   Match subject checks now pass inferred `TypeRef` nodes into wrapper-match
   detection, and match guard checks use typed expression inference before
   rendering only for diagnostics.
   Condition checks for `if`, `while`, and assert-like statements now use typed
   expression inference before rendering only for diagnostics and bool-operator
   lookup.
   Loop binding inference for non-name iterables now uses inferred `TypeRef`
   iterable metadata directly instead of rendering and reparsing the iterable
   type string.
   Compiler tuple destructuring now reads RHS tuple child `TypeRef` nodes and
   binds destructured locals with structured type metadata; alias/native spelling
   fallback remains at the compatibility boundary.
   LSP local-context tuple destructuring mirrors the structured compiler path,
   and the old public `tuple_types` string helper has been removed.
   Tuple child extraction with alias fallback now lives in a shared `TypeRef`
   helper, so compiler sema and LSP do not carry duplicate rendered-string tuple
   parsing logic.
   LSP local-context implicit bindings now use typed expression inference for
   untyped local declarations and first assignments, and the local string
   expression inference helper has been removed.
   LSP local-context `for` bindings now infer element types from stored iterable
   `TypeRef` metadata or typed iterable expression inference, and the local
   text-to-`TypeRef` adapter has been removed.
   TypeRef-backed assignment checks now infer RHS expressions as `TypeRef`
   first, use structured type assignment before compatibility fallback, and
   render only for legacy assignment/literal checks and diagnostics.
   Generic function/method inference now requires typed expression callbacks and
   no longer falls back to inferring rendered argument type strings and parsing
   them back into `TypeRef`.
   Body semantic checking now requires a typed expression callback, so statement
   checks cannot silently run without structured expression type inference.
   Array literal element checks now carry explicit and inferred element types as
   `TypeRef` nodes and try structured assignment before legacy literal
   compatibility fallback.
   Text-expected body type checks now infer RHS and generic-method receivers as
   `TypeRef` first, using structured type assignment before legacy
   assignment/literal compatibility fallback.
   Constructor argument validation now preserves field and `init` parameter
   `TypeRef` nodes, infers argument expressions as `TypeRef` first, and checks
   structural assignment before rendering types for compatibility diagnostics;
   constructor and `super.init` checking no longer accept a string expression
   inference callback.
   Native overload matching now accepts typed expression inference callbacks,
   checks argument/parameter `TypeRef` compatibility before rendered fallback
   paths, and binds native template placeholders from parsed argument types
   when available. Explicit native template placeholder discovery now walks
   parsed return and parameter `TypeRef` nodes before falling back to native
   spelling scans. Numeric promotion now checks parsed parameter and argument
   `TypeRef` nodes before falling back to rendered native spelling. The native
   signature matcher no longer accepts a string expression inference callback.
   Generic function and method inference no longer accepts a string expression
   inference callback; all inference inputs cross that boundary as `TypeRef`
   nodes.
   Body and match semantic callback APIs now expose typed expression inference
   only; the old string expression inference callback has been removed from
   those phase boundaries.
   Assignment target validation and side-effect-only statement checks now use
   typed expression inference for swizzle receivers, call targets, `range`
   arguments, assert/raise payloads, expression statements, and void-return
   value checks.
   Diagnostics and generated default assert messages now use an AST expression
   display helper for structured nodes instead of reading raw expression text
   directly. Member/index expression diagnostics and indexed-assignment labels
   use the same structured display path. Member-path semantic diagnostics now
   use structured expression display for parsed member/index labels, while the
   remaining string member-path API is still an explicit compatibility boundary
   to replace. The string resolver is now named
   `member_path_type_from_string`, with callers confined to the C++ escape
   inference boundary, and the helper declarations now live behind the
   expression-internal header rather than the public sema methods surface.
   Dudu-native constant aliases from selective imports now construct parsed
   `Name` expressions directly instead of reparsing imported identifier text
   inside the module loader. Semantic-token native lookups for member and
   callee expressions now derive dotted paths from parsed member nodes instead
   of reading the compatibility expression text field first, and no longer fall
   back to compatibility text when a parsed member path is unavailable. LSP
   symbol-at and reference collection locate member names from parsed receiver
   ranges instead of shifting by compatibility expression text width.

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
   Three-dimensional channel slices such as `image[:, :, c]` also produce
   `strided_span[T]` views over interleaved channel data.
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
   arguments. Static generic methods such as `Math.same[i32](42)` and
   `class.same[i32](value)` use the same instantiated signature path. Generic
   method type-argument arity is diagnosed in Dudu source.
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
   name matching and first-argument extraction. Decorator expression parsing
   now uses token-piece parsing directly instead of joined-text reparsing.
   Decorator parsing has regression coverage for string arguments containing
   operator characters, such as `@operator("+")`. String-valued compiler
   decorators now require exactly one parsed string literal argument, keeping
   `@operator(...)`, `@section(...)`, and `@test.should_panic(...)` off raw
   argument text fallback while preserving expression arguments for decorators
   such as `@align(...)` and `@workgroup_size(...)`.

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
   C++ import aliases now distinguish real scanned namespaces from Dudu-only
   header handles, so `std.vector` still lowers to `std::vector` while
   `native.Widget` from a global using alias lowers to `Widget`.
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
   ecosystem-facing toolchain. Current `duc emit` and direct-backend
   `dudu build` output one generated C++ translation unit for the whole Dudu source tree;
   source-tree module units are now preserved in the AST, and per-module
   semantic analysis validates those units without merged-name duplicate
   fallout. The emitter can produce `.hpp/.cpp` artifacts for each module unit.
   Those per-module artifacts opt into stable generated declaration names, so
   same-named declarations from different Dudu modules no longer collide in
   artifact declarations. Same-module parsed type references in those artifacts
   also lower through the generated type-name map for fields, parameters,
   returns, arrays, and templates. Same-module expression bodies now also lower
   function calls, constructors, constants, and other mapped declaration names
   through the generated value-name map. Qualified imported module references
   in per-module artifacts also lower to the imported module's generated C++
   names, so artifact bodies no longer preserve source-level `module.symbol`
   calls. `duc emit-modules` writes those artifacts to disk with a shared
generated runtime header, and the generated CMake backend now compiles the
per-module `.cpp` files instead of a merged generated translation unit. The
direct native build still compiles the compatibility single-file output and
fails clearly when that merged output cannot represent distinct module
declarations safely.

12. Language Server And Formatter

   Implement `duc lsp` so editors can show diagnostics, warnings, hover,
   completion, go to definition, find references, rename, formatting, and native
   header navigation. The concrete plan is
   [Language Server Plan](language-server-plan.md).

   Build LSP and formatter behavior from the same AST/sema model used by the
   compiler.

   Status: unaliased nested Dudu module imports such as
   `import vendor.helper` now resolve through their full dotted path for hover,
   go-to-definition, and member completion, matching the compiler's
   Python-shaped module binding behavior.

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

   The command-line UX must also be honest and observable. `dudu build`,
   `dudu run`, `dudu test`, and backend-driving commands should print useful
   progress and outcomes by default: selected backend, entry/module being
   built, generated artifact directory when relevant, compiler/CMake phase
   summaries, final executable path, run command, test counts, and clear skip
   messages when incremental checks reuse existing outputs. Quiet success with
   no useful stdout is not acceptable for normal user-facing commands. Add
   `--quiet` for scripts if needed, rather than making humans guess what
   happened.

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

   Status: `dudu build` and `dudu run` use the direct backend by default for
   single-module inputs. If no backend is explicitly selected and the source
   tree imports multiple Dudu modules, `dudu build` and `dudu run` select the
   generated CMake backend so generated C++ stays split into per-module
   artifacts. `[build] backend = "direct"` and `[build] backend = "cmake"` parse
   from the manifest, and explicit direct keeps strict merged-output diagnostics
   when a project cannot honestly fit in one generated translation unit. The
   generated CMake backend is implemented for `dudu build`, `dudu run`, and
   `dudu test`; it emits an internal CMake project and drives `cmake -S/-B`
   plus `cmake --build`. Native inputs such as include paths, library paths,
   libraries, flags, pkg-config packages, and extra C/C++ sources are partially
   implemented and useful. `dudu cmake` still emits CMake for inspection or
   handoff. User-owned CMake build/run/test are implemented for manifests that
   declare `[cmake] source` and `[cmake] target`; the driver configures and
   builds the existing CMake project under the Dudu build directory, and runs
   CTest when no Dudu test entry or explicit delegated test command is present.
   The generated CMake test backend still emits one generated harness
   translation unit; true per-module tests need test-mode module artifacts that
   expose test functions to the harness and suppress normal executable entry
   points in generated module sources.
   The serious path is to make these backend choices work behind the same
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

   Status: implemented. Semantic checking rejects runtime `assert` in
   `freestanding` and `embedded` target modes with a Dudu-source diagnostic
   that recommends `debug_assert` or a target-specific assert handler.
   `debug_assert` remains available and lowers to native C/C++ assertion
   behavior.

15. Macro Edge Cases

   Normal imported macros are covered. Keep token-pasting, declaration-
   generating, and partial-syntax macros behind wrapper headers unless a real
   library forces a better design.

   Status: implemented for the supported imported-macro surface. Fixtures cover
   object-like macros, function-like macros, direct lowercase macro calls such
   as `assert(expr)`, aliased lowercase calls such as `cassert.assert(expr)`,
   variadic macros with fixed leading-argument validation, stringizing-style
   passthrough macros, and mutation-style statement macros. Token-pasting,
   declaration-generating, and partial-syntax macros remain intentionally
   wrapper-header territory.

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

   Status: audited against the current implementation. Dunder protocol names
   are not accepted as compatibility aliases; they are reserved and diagnosed
   with guidance to use normal names and decorators such as `@operator(...)`.
   `@staticmethod`, `@classmethod`, and `@property` are rejected instead of
   being kept as alternate OOP spellings. Prototype Python expression sugar
   such as `lambda`, ternary conditional expressions, comprehensions,
   generator expressions, `yield`, `with`, and RHS `def` expressions has
   negative fixture coverage and is rejected through the unsupported-feature
   path. The remaining raw machinery is documented as either the explicit
   `cpp(...)` escape boundary, native C/C++ spelling compatibility, or the
   intentionally narrow direct-backend merged translation unit. No active
   dunder/operator compatibility path or stale inline-function-literal path was
   found in the implementation.
