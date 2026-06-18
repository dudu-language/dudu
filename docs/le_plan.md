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

   The near-term target is a typed/core AST, also commonly called HIR, not a
   separate low-level optimizer IR. Dudu is primarily a source-to-source systems
   language targeting readable C++ today, so codegen should consume structured
   AST/HIR nodes and semantic facts directly. A lower-level IR becomes
   appropriate when there is a concrete need for optimizer passes, non-C++
   native backends, deeper dataflow analysis, or multiple backend families. Do
   not add a low-level IR just to hide remaining parser/sema/codegen string
   shortcuts; remove those shortcuts at the AST/HIR boundary instead.

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

Status: diagnostics now parse/analyze the project tree before linting, and the
lint entry point consumes `ModuleAst` plus `Document` rather than raw source
lines. AST lint traversal uses shared AST expression walkers instead of
hand-maintained statement expression slot lists. Suspicious casts, raw
`cpp(...)` warnings, shadowing, unused locals, and unreachable-code checks all
walk structured statement/expression nodes. The unreachable-code lint uses
structured control-flow shape for `if`/`elif`/`else` chains, reports only the
first unreachable statement in a block, and now lives in a focused AST-backed
pass with shared source-file identity handling. Suspicious-cast lint scope
state carries structured `TypeRef` locals and renders type text only at the
diagnostic message boundary. Remaining work is to keep splitting mixed
lint/code-action logic, move any residual edit construction onto concrete
AST/token ranges, and add more false-positive fixtures for realistic
project/module cases. Import organization in the formatter and LSP code
actions now shares a parsed `ImportDecl` renderer from the AST layer instead
of carrying duplicate import string builders, and generic document line helpers
live in LSP support instead of inside code actions.

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
the next sema/codegen work can operate module-by-module. Each module unit now
also carries resolved dependency metadata for Dudu imports, including the source
import spelling, canonical resolved module path, and resolved source file.
Same-name declarations now also carry stable generated C++ names derived from
their owning module, but the direct backend and semantic lookup still need to
switch to those names coherently before they can coexist through normal
semantic analysis and codegen. The per-module artifact emitter now uses those
generated names for same-module declarations and qualified imported module
references, including `import module as alias`, `import module.path`, and
selective `from module import Name` forms. `duc emit-modules` now analyzes each module unit in its
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
   statement source ranges retained as token spans and explicit `cpp(...)`
   escape bodies stored separately. The raw `Stmt::text` and `Stmt::source_text`
   fields have been removed.
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
   `struct Foo`. Wrapper match payload bindings now carry structured `TypeRef`
   metadata through sema/codegen instead of reparsing rendered payload strings,
   and compound assignment statements now carry a structured operator enum
   instead of a raw operator string. Statement typed declarations, typed loops,
   and typed catches now rely on `Stmt::type_ref`; the statement-level raw type
   mirror has been removed. Unsupported statement forms now carry a structured
   unsupported-feature enum instead of diagnostic text on the AST node. Class
   field initializers now rely on `FieldDecl::value_expr`; the raw field
   initializer string mirror has been removed. Module constants, class
   constants, and static fields now rely on `ConstDecl::value_expr`; the raw
   constant initializer string mirror has been removed. Enum value initializers
   now rely on `EnumValueDecl::value_expr`; the raw enum initializer string
   mirror has been removed. Static asserts now rely on
   `StaticAssertDecl::expression_expr`; the raw expression string mirror has
   been removed, and static assert failure diagnostics render the parsed
   expression tree instead of reading raw expression text. Dudu-declared
   function return types and out-of-line method receiver types now live as
   `TypeRef` nodes; string return signatures remain only at native/import
   compatibility boundaries such as scanned C++ header functions.
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
   a member path string first. C++ escape expression inference now prefers
   parsed member expressions, parsed member-call callees, parsed index
   expressions, parsed tuple expressions, parsed name expressions, and parsed
   collection literal nodes before the remaining explicit native spelling
   fallback paths. The parsed C++ escape call/member helper code has been
   split into a focused helper module so the boundary logic does not keep
   growing inside expression sema. Nested expression emission preserves symbol
   context through callee, member, dict-entry, named-argument, index,
   collection literal, tuple, template-call argument, swizzle, pointer-cast,
   and fixed-array literal children. Iterable/indexed-local helpers now accept
   local `TypeRef` metadata directly instead of carrying rendered local type
   strings through APIs that no longer read them, and emitted-local type
   inference no longer accepts the rendered local type map. Address-escape
   semantic checks now classify local storage from `TypeRef` metadata instead
   of reparsing rendered local type strings. Destructuring shadow checks and
   inferred-assignment local existence checks now use local `TypeRef` metadata
   instead of rendered type strings. Local existence checks in semantic
   call/member handling now use `local_type_refs`, and member-path type helpers
   no longer accept the rendered local type map. Enum payload match bindings
   now go through the shared local-binding helper instead of writing rendered
   and structured local maps by hand. LSP local-context collection now keeps
   local `TypeRef` metadata as the primary result and renders string types only
   at hover/completion compatibility boundaries. Slice/swizzle and pointer-call
   emission helpers now avoid unused rendered local type parameters when the
   decision comes from local `TypeRef` metadata. Enum variant recognition now
   uses a shared structural expression helper in sema, codegen, and match
   patterns instead of
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
   Parsed call and template-call expressions no longer receive a parser-filled
   callee-name mirror; sema, codegen, lints, and AST tests read direct call
   identity through the structured callee expression. Shared callee helpers now
   return an empty callee for malformed internal call nodes instead of falling
   back to stale `Expr::name` mirrors.
   `super.init(...)` recognition in sema and class emission now checks parsed
   member-callee shape instead of reconstructed callee text. Type
   compatibility exposes parsed `TypeRef`
   overloads, and annotated local initializers validate against the parsed
   expected type node when available. `TypeRef` assignment compatibility keeps
   `list`, `set`, `dict`, `Option`, and `Result` expected types on parsed
   template-child nodes for local initializer checks instead of rendering those
   shapes back to strings first. Type-to-type compatibility now structurally
   matches parsed pointer, reference, wrapper, template, and function type
   nodes before falling back to native spelling compatibility.
   Pointer/reference compatibility now handles void pointer targets, const
   pointer binding, pointer-to-reference values, value from reference/const
   wrappers, and native function-pointer compatibility through parsed
   `TypeRef` nodes before native spelling fallback. Typed `for`
   loop binding checks now compare against the parsed binding `TypeRef` before
   using alias/native spelling fallback, and render fallback diagnostics from
   the parsed binding `TypeRef` instead of compatibility type text. Delete/free
   checks now classify
   pointer arguments through parsed `TypeRef` nodes instead of raw leading `*`
   spelling. Indexed type inference now requires parsed index expressions
   instead of accepting public string index text, including the explicit
   `cpp(...)` escape inference path. Fixed-array index and slice metadata now
   reads parsed `TypeRef` shape/element nodes, and the structured index helper
   handles aliases, foreign/auto receivers, and Dudu `[]` operator return
   metadata without rendering `TypeRef` input back into source text and
   reparsing it. Iterable inference now follows the same direction: the
   `TypeRef` result path extracts local iterable element types directly from
   parsed local metadata or one parsed declared local type, instead of calling
   the string-result helper and parsing the rendered element type. Assignment
   target inference now peels pointer pointee types and swizzle assignment
   result types as `TypeRef` nodes instead of parsing rendered target type
   strings.
   Native C++ type-artifact normalization now has a parsed `TypeRef` entry
   point, so tuple-element and non-array template cleanup no longer require
   parsed callers to stringify only to reparse immediately.
   String-facing index type inference now parses the resolved input type once
   and reuses that `TypeRef` for shape/slice/index checks before crossing the
   remaining native/operator spelling fallback. Index and iterable inference
   now unwrap reference/const receivers through parsed `TypeRef` nodes instead
   of reparsing rendered unwrapped text. Emitted local index type
   inference now tries parsed `TypeRef` handling before falling back to
   native-spelling string template extraction. Template call
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
   Shared `TypeRef` wrapper construction now builds pointer, reference, and
   const wrapper nodes structurally across sema and codegen, with text rendering
   kept at explicit C++/diagnostic boundaries. Named, tuple, function,
   fixed-array, slice/span, result-wrapper, and native tuple-index return types
   now rely on structured `TypeRef` fields instead of prefilled rendered text
   mirrors. Recursive substitution now keeps substituted `TypeRef` results
   structured as well, so derived `TypeRef.text` writes are gone from ordinary
   sema/codegen/type-substitution paths.
   Templated pointer-cast semantic inference also builds the pointee from
   parsed template `TypeRef` arguments instead of reconstructing `Name[...]`
   text and reparsing it. Inferred array literal shapes now carry a
   fixed-array `TypeRef`, so sema and C++ emission bind inferred array locals
   without regenerating `array[T][...]` text and reparsing it. Local
   declaration sema and C++ emission now carry an effective declared `TypeRef`
   for shaped-array inference, instead of deciding parsed metadata validity by
   comparing rendered type strings. LSP local binding uses the same
   shaped-array `TypeRef` inference for annotated locals, so editor facts
   follow the compiler's effective declaration type. Typed catch and loop
   bindings now store local types from parsed `TypeRef` metadata in sema,
   codegen, and LSP local scopes instead of using raw annotation strings as
   semantic local types. Suspicious-cast lint facts for annotated locals and
   parameters now render from parsed `TypeRef` nodes instead of raw annotation
   strings, and typed local/catch/loop/lint paths now test parsed `TypeRef`
   presence instead of non-empty raw annotation text. Generic
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
   Assignment C++ emission now uses local `TypeRef` metadata to decide whether
   a name is an existing local, and first assignments with unknown native escape
   types store an explicit `auto` `TypeRef` instead of relying on rendered local
   strings.
   Explicit `cpp(...)` pointer-member rewrites for expression and statement
   escapes now read local pointer/list-pointer facts from `TypeRef` metadata
   instead of reparsing rendered local type strings.
   Statement block C++ emission no longer exposes string-only local-type
   overloads that parse rendered locals back into `TypeRef`; callers must pass
   structured local metadata alongside the compatibility local-name map.
   The type parser now recognizes C++ scoped template spellings such as
   `std::vector<std::string>` as structured `Template` `TypeRef` nodes, and
   builtin method inference uses those parsed children instead of owning a
   separate native-template substring splitter.
   Receiver-template substitution now has a `TypeRef` path, so method and field
   instantiation can substitute `value_type`/`element_type` placeholders without
   rendering substituted types to strings and reparsing them.
   Inherited method signature instantiation now has a `TypeRef` receiver path,
   so generic base/interface methods substitute receiver arguments without
   reparsing the rendered receiver type.
   Inherited method lookup now accepts parsed receiver `TypeRef` nodes, and
   override validation passes parsed base-class references through that path
   instead of rendering base types for lookup.
   Class instance-storage queries now accept parsed `TypeRef` receivers, so
   super/base-class emission can inspect generic base storage without rendering
   base types first.
   Native base assignability now accepts parsed expected/got `TypeRef` nodes,
   so typed assignment checks can validate derived-to-base pointer/reference
   assignments without rendering both sides first.
   Receiver unwrapping now accepts parsed `TypeRef` nodes, and inherited field
   lookup plus swizzle lookup use that path when the receiver/base type is
   already structured.
   Instance method signature lookup now accepts parsed receiver `TypeRef`
   nodes, and inherited method recursion walks parsed base-class references
   instead of rendering base types before lookup.
   Instance method signature collection now accepts parsed receiver `TypeRef`
   nodes, so overload-list collection walks inherited base-class references
   without rendering base types before recursion.
   Static method signature lookup now accepts parsed receiver `TypeRef` nodes,
   so inherited static lookup walks parsed base-class references without
   rendering base types before recursion.
   Generic method inference now accepts parsed receiver `TypeRef` nodes for the
   normal inference path, so inherited generic method lookup walks parsed
   base-class references without rendering base types before recursion.
   Expected-return generic method inference now accepts parsed receiver
   `TypeRef` nodes, so contextual return inference for inherited generic
   methods walks parsed base-class references without rendering base types
   before recursion.
   Expected-type generic method C++ emission now keeps receiver inference as
   parsed `TypeRef` metadata and walks inherited base-class references without
   rendering receiver/base types before template argument inference.
   Inheritance traversal helpers for derives-from checks, abstract-method
   resolution, inherited field/method collection, storage checks, and method
   declaration lookup now walk parsed base-class `TypeRef` nodes instead of
   rendering base types before recursion.
   `super` method and `super.init` inference now keep selected base classes as
   parsed `TypeRef` nodes, passing structured receiver types into constructor
   and method checks and rendering only for diagnostics.
   Array literal shape inference now carries the inferred element type as a
   parsed `TypeRef`, and semantic element checks use that structured metadata
   instead of reaching back through rendered declaration text.
   Iterable binding inference now exposes only `TypeRef` helpers; the remaining
   string-only iterable element APIs were removed, and LSP local binding
   inference uses the same structured iterable element path as semantic checks.
   Indexed local inference now exposes only the `TypeRef` helper; `cpp(...)`
   escape inference uses structured indexed-local metadata internally and
   renders only at the escape compatibility boundary.
   Loop binding compatibility now resolves aliases through `Symbols::alias_type_refs`
   and compares parsed `TypeRef` nodes instead of resolving binding and element
   types through string aliases.
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
   spelling scans. Explicit native template substitution rewrites parsed
   parameter and return `TypeRef` nodes first when the signature has ordinary
   template placeholders; native C++ pack/decay spellings stay on the explicit
   native spelling path until the pack has been matched. Numeric promotion now
   checks parsed parameter and argument `TypeRef` nodes before falling back to
   rendered native spelling. The native signature matcher no longer accepts a
   string expression inference callback.
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
   use structured expression display for parsed member/index labels.
   Member-path inference has one structured source of truth for local names,
   class static members, recursive fields, indexed receivers, inherited
   fields, `Result` helper fields, and swizzles; remaining string-returning
   member and field helpers render the structured `TypeRef` result for
   compatibility boundaries instead of maintaining a parallel resolver.
   Dudu-native constant aliases from selective imports now construct parsed
   `Name` expressions directly instead of reparsing imported identifier text
   inside the module loader. Semantic-token native lookups for member and
   callee expressions now derive dotted paths from parsed member nodes instead
   of reading the compatibility expression text field first, and no longer fall
   back to compatibility text when a parsed member path is unavailable. LSP
   symbol-at and reference collection locate member names from parsed receiver
   ranges instead of shifting by compatibility expression text width. Shared
   AST traversal helpers now own expression-tree and statement-expression
   walking, and the AST lint, unsupported-feature, build-flag, and navigation
   passes use those helpers instead of maintaining hand-written expression-slot
   lists.
   Loop binding, assignment compatibility, member-path receiver unwrapping,
   `Result[...]` field lookup, indexed type inference, and native template
   binding now resolve aliases through parsed `TypeRef` metadata first, with
   old string alias maps retained only at explicit compatibility boundaries.
   Inheritance receiver/base unwrapping now follows the same typed-first alias
   resolution path for base assignability and inherited lookup.
   Builtin C++ method signature matching now normalizes receiver aliases and
   pointer/reference wrappers through parsed `TypeRef` metadata before deriving
   container/optional/atomic method signatures.
   Native overload candidate diagnostics render parameter and return types
   through signature `TypeRef` helpers rather than raw string mirrors.
   Native overload assignment callbacks now carry expected and actual argument
   types as parsed `TypeRef` nodes instead of string type names.
   Generic inference callbacks and constructor argument assignment callbacks
   now carry parsed expected/actual `TypeRef` nodes too. Constructor errors
   still render readable type names at the diagnostic edge, but matching no
   longer has to stringify the type pair before asking semantic assignability.
   The duplicate string-only indexed type implementation has been removed;
   public string index queries parse the receiver once and delegate to the
   structured `TypeRef` indexing implementation.

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
   preserve parsed expression nodes without a raw text mirror, and
   compiler-recognized decorators use a shared parsed-expression helper for
   name matching and first-argument extraction. Decorator expression parsing
   uses token-piece parsing directly.
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
   fallout. Module units now carry resolved Dudu dependency metadata, and the
   per-module artifact emitter uses that metadata for generated includes and
   imported generated-name lookup instead of deriving dependencies from raw
   import spelling. The emitter can produce `.hpp/.cpp` artifacts for each
   module unit. Those per-module artifacts opt into stable generated
   declaration names, so same-named declarations from different Dudu modules no
   longer collide in artifact declarations. Same-module parsed type references
   in those artifacts also lower through the generated type-name map for
   fields, parameters, returns, arrays, and templates. Same-module expression
   bodies now also lower function calls, constructors, constants, and other
   mapped declaration names through the generated value-name map. Qualified
   imported module references in per-module artifacts also lower to the
   imported module's generated C++ names, so artifact bodies no longer preserve
   source-level `module.symbol` calls. `duc emit-modules` writes those artifacts
   to disk with a shared generated runtime header, and the generated CMake
   backend now compiles the per-module `.cpp` files instead of a merged
   generated translation unit. The direct native build still compiles the
   compatibility single-file output and fails clearly when that merged output
   cannot represent distinct module declarations safely.

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
   found in the implementation. Optional expression holes now use a dedicated
   `Missing` expression kind; `Unknown` is reserved for unsupported source text
   that should be diagnosed rather than interpreted.

17. Delete AST Migration Fallbacks

   After each language form has structured parser, semantic, codegen,
   diagnostic, lint, LSP, and formatter coverage, remove the compatibility
   mirrors that existed only to keep the compiler green during migration.

   This includes fallback string callbacks such as assignability hooks that
   receive rendered type names, generic inference hooks that pass text instead
   of `TypeRef`/AST nodes, raw statement/value mirrors that duplicate
   structured `Stmt` and `Expr` fields, string-only local/type maps when a
   `TypeRef` map is authoritative, and parser helpers that reparse text that
   was already tokenized. Explicit user escape hatches such as `cpp(...)` may
   remain, but compiler-internal fallback APIs should be deleted once their
   callers have structured equivalents.

   Fallback string callbacks are migration scaffolding only. After the
   structured AST path owns a behavior, the matching callback must be removed
   rather than kept as a parallel implementation, callback adapter, quiet
   recovery path, or legacy compatibility layer. Finish this milestone with a
   codebase-wide audit for fallback string callbacks, callback adapters, and
   compatibility mirrors, and delete every compiler-internal one that is no
   longer an explicit native/C++ boundary.

   The old `resolve_alias_ref_with_legacy_fallback` migration helper has been
   deleted. Alias resolution must consume structured `TypeRef` alias metadata
   directly; do not reintroduce rendered string alias fallback as a permanent
   alias-resolution path.

   Definition of done: no semantic, codegen, lint, LSP, formatter, or test
   discovery path may keep a fallback callback that accepts rendered source or
   rendered type strings when structured `Stmt`, `Expr`, `TypeRef`, tokens, or
   symbol-table facts are available. These APIs should fail loudly during the
   migration if a caller has not been ported yet; they should not silently
   preserve the old string path.

   Final audit: search explicitly for fallback string callbacks, callback
   adapters, `std::function` hooks that traffic in rendered source/type text,
   and helpers whose only job is to stringify structured AST data so it can be
   parsed again. Remove every compiler-internal instance unless it is an
   explicit native/C++ boundary or user-authored escape hatch.

   Status: in progress. Body, generic, constructor, and native assignment
   paths are being migrated from rendered string type pairs to parsed
   `TypeRef` pairs. Constructor parameter checking no longer stores a duplicate
   rendered type string beside `TypeRef`, and array shape inference no longer
   stores duplicate rendered array or element type strings beside its inferred
   `TypeRef` nodes. Native template binding no longer reparses expected/got
   strings for its alias-aware path. Enum payload checking and operator
   overload matching now use parsed expected-side `TypeRef` metadata for
   assignment compatibility instead of rendered type strings. The remaining
   compatibility callbacks and mirrors should be removed rather than normalized
   into permanent API.

   LSP local completion now consumes `local_type_refs_before_cursor` directly
   and renders completion detail text at the UI boundary; the rendered
   `local_types_before_cursor` wrapper has been removed.
   Hover, member completion, and member definition lookup now use
   `local_type_ref_before_cursor`, leaving rendered local type strings at the
   UI/member-candidate display boundary.
   Inheritance override keys and signature equality now render through
   `FunctionSignature` `TypeRef` helpers instead of comparing signature string
   mirrors directly.
   Function scopes no longer carry a rendered local type string map; semantic
   checks and LSP local lookup bind locals through `TypeRef` metadata only.
   Assignment target semantic typing exposes only the structured `TypeRef`
   helper; the old string-returning wrapper has been removed.
   Semantic var-decl effective type calculation no longer stores a rendered
   type string beside the authoritative `TypeRef`; inferred array local type
   validation now uses `check_known_type_ref` directly.
   AST type child helper APIs no longer expose string-returning template or
   unary child accessors; callers must consume `TypeRef` children and render
   only at explicit display, native compatibility, or C++ emission boundaries.
   Parser token spans no longer carry duplicate exact source text for
   statement/expression parsing; exact source reconstruction is isolated to
   import declarations where LSP import-code actions need to preserve the
   original line.
   Parser joined-token spans no longer synthesize normalized expression/type
   text while parsing declarations or statements; expression/type parsers now
   consume the token slice directly, with exact source reconstruction reserved
   for explicit `cpp(...)` escape bodies and import source preservation.
   Expression semantic checking, C++ emission, and unsupported-feature
   detection no longer treat empty `Expr::text` as an absent expression;
   absence is represented by the structured `ExprKind::Missing` node, while
   `ExprKind::Unknown` remains a diagnosed unsupported expression.
   Native overload matching no longer caches a rendered argument type beside
   the inferred `TypeRef`; diagnostics and remaining native-template fallback
   bindings render on demand at their boundary.
   C++ statement emission effective local type calculation no longer stores a
   rendered type string beside `TypeRef`; the legacy string locals map is
   populated by rendering at the emission boundary only.
   The C++ emitter no longer stores rendered type names for ordinary function
   params, method params, local declarations, implicit assignment locals, or
   loop bindings in its legacy `locals` map. That state is now an explicit
   `CppLocalContext` with local-name presence plus dedicated `current_class`
   and `super_class` lowering fields; `local_type_refs` remains the
   authoritative local type table.
   Explicit fixed-array shape extraction no longer renders the array element
   type just to read dimensions; it now returns structured shape data only.
   Indexing type inference exposes only parsed `TypeRef` receiver APIs; the old
   string receiver and string result wrappers have been removed, with
   `cpp(...)` escape inference rendering only at its boundary.
   Array shape inference and fixed-array introspection now accept parsed
   `TypeRef` declarations only; dead string overloads and unused string element
   helpers have been removed.
   General AST type helper convenience overloads that parsed raw strings have
   been removed; callers must parse once and pass `TypeRef`.
   Unused string reparsing overloads for function-type parsing and receiver
   template substitution have been removed; tests now exercise those paths by
   parsing once and passing `TypeRef`.
   Native member lookup exposes the parsed `TypeRef` result API only; unused
   string-returning native member helpers have been removed.
   Dudu method lookup, generic method inference, static method lookup, and
   receiver unwrapping now expose parsed `TypeRef` receiver APIs only; remaining
   explicit C++ escape paths parse receiver strings at the boundary.
   Field and swizzle lookup now expose parsed `TypeRef` receiver APIs only; the
   unused string-returning wrappers have been removed.
   Member-expression typing now exposes the parsed `TypeRef` API only; the
   explicit C++ escape boundary renders the result locally.
   Emitted local call-return inference no longer has an internal receiver-base
   string fallback that renders a type and peels brackets; it relies on parsed
   `TypeRef` shape and `type_ref_head_name`.
   Receiver template argument extraction now accepts parsed `TypeRef` only; the
   unused string overload that reparsed rendered receiver types has been
   removed.
   Built-in `min`/`max` and contextual numeric binary inference now pass
   inferred `TypeRef` operands into assignment compatibility instead of calling
   through rendered argument-type strings.
   Dudu-owned `FunctionSignature` construction now uses centralized
   `TypeRef` setters for parameter and return types; rendered signature strings
   are derived at that assignment boundary instead of being hand-maintained by
   function, method, generic, inheritance, operator, and builtin-method
   builders. Native C++ signatures keep their spelling strings as an explicit
   imported-library boundary.
   Body expression assignment checking now exposes only the parsed
   `TypeRef`/`TypeRef` `can_assign_ast` API; the string and mixed string
   overloads have been deleted.
   `Result` literal compatibility now parses `Ok[T]`/`Err[E]` result types
   and reads template children instead of slicing rendered type strings.
   Binary and comparison RHS validation now expose parsed `TypeRef` APIs; they
   unwrap aliases through structured type nodes and render strings only for
   native/foreign compatibility checks and diagnostics.
   Binary operator signature lookup now accepts parsed operand `TypeRef` nodes;
   Dudu operator matching and native operator argument checks no longer reparse
   rendered left/right operand type strings.
   Dudu operator lookup for `bool`, `[]`, and `[]=` now takes parsed receiver
   `TypeRef` nodes; index, assignment-target, condition, and `cpp(...)` escape
   checks no longer render receiver types before querying operator metadata.
   General type assignment compatibility now exposes only the parsed
   `TypeRef`/`TypeRef` public API; the former public string/string helper is a
   private native-artifact compatibility helper.
   Expression-aware assignment compatibility no longer exposes an
   expected-type string overload; value-wrapper recursion parses its inner type
   before re-entering the structured assignment path.
   Assignment diagnostics now expose only the parsed expected-`TypeRef` API;
   string formatting is kept private to the diagnostic renderer.
   General base-type queries now expose a parsed `TypeRef` API; the old public
   raw-string helper has been removed, and remaining callers parse only at
   explicit text/native boundaries.
   Inheritance and abstract-construction checks now expose parsed `TypeRef`
   APIs for native base assignability, instance-storage queries, inherited
   method lookup, and unimplemented abstract method checks; the former public
   string overloads and string reparsing wrappers have been deleted.
   Built-in C++ method signature lookup now accepts parsed receiver `TypeRef`
   nodes; the old public rendered receiver-type API and receiver template
   string helper have been deleted.
   Native signature matching now keeps inferred argument types and expected
   parameter types as `TypeRef` nodes for assignability and tuple-indexed
   return handling; rendered argument type strings remain only for diagnostics
   and native template fallback binding.
   Void return checks now use an AST-level `type_ref_is_void` helper, and
   module entry-point emission no longer renders a function return type string
   just to compare it with `void`.
   Function parameter parsing now carries receiver `TypeRef` nodes into
   implicit `self` parameter synthesis instead of rendering the receiver type
   string and rebuilding the parameter type from text.
   Auto-type checks now use an AST-level `type_ref_is_auto` helper in
   assignment-target validation, generic method emission, indexability checks,
   and member path lookup instead of rendering `TypeRef` nodes just to compare
   them with `auto`.
   Function-type parsing now uses the AST-level `type_ref_is_void` helper when
   recognizing chained function type syntax instead of inspecting a child
   type's raw text field.
   Named-type checks now use a shared AST-level `type_ref_is_name` helper;
   declaration and body semantic helpers no longer carry duplicate local
   implementations.
   Binary/comparison expression semantic checks also use the shared
   `type_ref_is_name` helper instead of carrying a local duplicate.
   TypeRef pointer compatibility checks now unwrap unary type wrappers
   structurally and use `type_ref_is_void` for void-pointer and native function
   pointer checks, leaving rendered comparisons only in the explicit legacy
   string compatibility overloads.
   Native template compatibility now recognizes `basic_string[char]` through a
   structured `TypeRef` child-name check instead of rendering the child type to
   compare it with `char`.
   Legacy string alias fallback after structured alias resolution has been
   removed. Native template matching, inheritance unwrapping, member-path
   alias resolution, Result field lookup, and index inference now consume
   structured `TypeRef` aliases directly instead of rendering and reparsing
   type spellings.
   `ImportDecl::source_text` has been removed; the LSP organize-imports action
   now renders canonical import lines from structured `ImportDecl` fields
   instead of storing a raw source-line mirror in the AST.
   Statement AST shape tests now assert structured expression nodes for normal
   statements and match cases instead of treating raw expression text as the
   canonical AST contract; raw text checks remain only for source-spelling
   cases such as numeric literal spelling and explicit `cpp(...)` escapes.
   Parser statement range attachment is now named as range attachment rather
   than source attachment, matching the fact that raw statement source mirrors
   have been removed.
   Type-known checks now expose a structured `known_type_ref` API, and
   unknown-type diagnostics use it directly instead of reparsing rendered type
   strings. Native/foreign C++ receiver checks also accept `TypeRef` nodes so
   normal member/method inference no longer renders receiver types just to
   recognize native C++ types.
   Type presence checks in local binding, C ABI validation, enum emission,
   function signature return access, and generic return binding now use
   `has_type_ref` rather than open-coding `TypeKind::Unknown` plus raw text
   checks.
   Local function type parsing and function-type missing-return checks now also
   use `has_type_ref`; the remaining raw text presence checks are confined to
   parser/type rendering, LSP display, and the helper implementation itself.
   `resolve_alias_ref` now expands aliases recursively inside structured
   `TypeRef` children. Indexed-type alias resolution uses that structured
   resolver directly, so list/dict/array indexing no longer depends on the
   legacy render/resolve/reparse alias fallback.
   Inheritance/base assignability unwrapping also uses structured alias
   resolution plus `type_ref_equivalent`; pointer/reference base checks no
   longer route through the legacy alias fallback helper.
   Foreign C++ type detection now resolves `TypeRef` aliases internally and
   unwraps unary pointer/reference/qualifier wrappers structurally, so member
   and member-call inference pass receiver `TypeRef`s directly instead of
   wrapping each call with the legacy alias fallback helper.
   Function signatures now expose `signature_param_count`, and regular call
   checking, function-type construction/rendering, and native overload arity
   diagnostics use it instead of reading the legacy parameter string vector
   directly. Native pack-placeholder handling still reads the final parameter
   spelling at the native text boundary.
   Operator and inheritance signature checks now use `signature_param_count`
   for arity and inherited signature construction, leaving direct
   `signature.params` access mostly in native signature substitution/pack
   spelling boundaries and the legacy fallback accessors.
   Native function symbol collection now populates `FunctionSignature` through
   `set_signature_param_types` and `set_signature_return_type` instead of
   manually assigning the string and `TypeRef` mirrors in parallel.
   Native overload fallback signatures also use `set_signature_return_type`
   for `auto` returns instead of writing the return string and `TypeRef`
   fields separately.
   Native template substitution still operates at a native spelling boundary,
   but its refresh step now reparses the substituted strings through
   `set_signature_param_types` and `set_signature_return_type` instead of
   manually synchronizing the string and `TypeRef` mirrors.
   Function signatures now expose explicit text-boundary setters for parameter
   and return type spellings; native substitution refresh uses those APIs
   rather than parsing and assigning mirror fields itself.
   Explicit and bound native template substitution now routes transformed
   parameter and return types through the signature setters directly; the old
   local refresh helper has been deleted.
   Function-type parsing now also routes parsed return and parameter
   `TypeRef`s through the signature setters, removing another hand-synced
   string/`TypeRef` mirror write.
   Expression codegen now lowers boolean and numeric literals from structured
   literal `value` fields, with raw text retained only as a spelling fallback;
   AST tests assert those literal values explicitly.
   Literal assignment compatibility now has a `TypeRef` got-type path for
   `Result` and `Option` wrapper literals; the string overload remains only as
   a compatibility shim for callers that have not crossed the native/text
   boundary yet.
   Literal assignment compatibility now exposes only the `TypeRef` got-type
   entry point; the last string overload in that layer has been deleted, with
   any remaining string compatibility parsing kept at the outer
   `assignment_type_allowed` boundary.
   The public `assignment_type_allowed` API now accepts only structured
   `TypeRef` got-types; its string overload and the dependent wrapper/variant
   helper paths have been removed rather than preserved for tests.
   Null literal pointer assignment compatibility now checks pointer shape and
   `None` through `TypeRef` nodes instead of rendering expected/got types to
   strings.
   The duplicate `Symbols::aliases` rendered string map has been deleted.
   Alias resolution now uses `alias_type_refs` as the single source of truth
   and renders only at string-returning compatibility call sites.
   The dead `Symbols::native_values` rendered string map has been deleted;
   semantic lookup uses `native_value_type_refs` directly, while
   `ModuleAst::native_values` remains the declaration list for code emission.
   `FunctionSignature` no longer stores rendered parameter or return type
   mirrors. Signature construction and tests now use `TypeRef` fields plus
   accessor helpers as the only semantic representation.
   Operator and condition semantic diagnostics now use `FunctionSignature`
   accessors for parameter counts and parameter type text instead of reading
   the legacy signature string vector directly.
   Function signatures now expose a parameter text accessor beside the return
   text accessor. Native matching and substitution use those signature APIs,
   leaving direct access to the legacy parameter/return string mirrors confined
   to `sema_function_type.cpp`, where the temporary mirror boundary is
   implemented.
   Builtin container/optional/atomic method signatures now carry extracted
   item, key, and value types as `TypeRef` nodes instead of rendering template
   children to strings and reparsing them when constructing signatures.
   Native explicit-template indexed tuple returns now inspect the return
   `TypeRef` shape directly and update matched signatures through
   `set_signature_return_type`; the old path rendered the return placeholder,
   sliced the string, and manually synchronized return mirrors.
   Native operator signature receiver dropping now rebuilds the remaining
   parameter list from `TypeRef` accessors and writes it back through
   `set_signature_param_types`; direct parameter mirror mutation is confined to
   the signature helper implementation.
   AST frontend tests now assert `FunctionSignature` parameters and returns
   through structured accessors rather than treating the legacy string mirrors
   as the public contract.
   Native variadic pack matching now reads the final parameter through
   `signature_param_type_text`; direct `FunctionSignature` mirror access in
   production code is confined to `sema_function_type.cpp`.
   `NativeValueDecl` now carries a `TypeRef` beside the imported spelling.
   Header scanning, module constant imports, symbol collection, and LSP symbol
   detail use the structured type when available instead of reparsing the
   native value type string.
   `NativeTypeDecl` now carries a `TypeRef` for alias targets. Native header
   typedefs, module import aliasing, prefixed native symbols, symbol
   collection, and LSP local alias expansion use structured type metadata when
   available instead of reparsing the native type string.
   Tuple destructuring now resolves aliased tuple types through structured
   `alias_type_refs`; the old string-alias overload has been deleted, and the
   remaining helper is explicitly `template_type_arg_refs_with_aliases`.
   Override signature matching now compares parameter and return `TypeRef`
   structure through `type_ref_equivalent` instead of rendering signatures to
   strings for semantic equality.
   Function signature construction no longer exposes general-purpose
   string-type setters; native template substitution parses at the native
   boundary and then updates signatures through `TypeRef` setters.
   `BodyCheckCallbacks` no longer carries a call-argument checking callback;
   sema body helpers and assignment target checks call the structured
   `check_call_args_ast` path directly.
   `SuperCheckCallbacks` likewise no longer carries a call-argument checking
   callback; `super` method validation now calls the structured checker
   directly after overload selection.
   `BodyCheckCallbacks` and `expression_body_check_callbacks` have been
   deleted entirely; body checking, body helper checks, assignment target
   checks, and LSP local inference now call the structured expression
   inference and assignment compatibility APIs directly.
   `GenericInferCallbacks` has been deleted entirely; generic function and
   method inference now use the normal structured expression inference path
   directly, and tests set up real scope type facts instead of callback-only
   fake inference.
   Constructor argument checking no longer accepts expression-inference or
   assignment callbacks; constructor call sites, including `super.init`, now
   use the structured expression and assignment paths directly.
   `SuperCheckCallbacks` has been deleted entirely; `super` method validation
   now calls structured constructor checking, overload matching, argument
   checking, and return-type access directly.
   Match checking no longer accepts an expression-inference callback; match
   subjects and guards now use the normal structured expression inference path
   directly, leaving only the recursive block-check hook for nested case bodies.
   Native function signatures now expose structured accessor helpers for
   parameter and return `TypeRef`s plus rendered display text. Symbol
   collection, native function deduplication, and LSP symbol details use those
   accessors instead of open-coding `NativeFunctionDecl::params` /
   `return_type` fallback logic.
   Native type aliases and values now expose structured accessor helpers as
   well. Symbol collection and LSP native symbol/local context code use those
   helpers instead of open-coding `type_ref`-or-rendered-string fallback logic.
   Type substitution now uses a structured lookup key from `TypeRef` kind/name
   metadata, falling back to raw text only for unknown/raw type refs. This
   removes another general-purpose dependency on rendering whole types before
   applying substitutions.
   Template argument extraction through aliases now uses the same structured
   `TypeRef` lookup key instead of rendering the whole candidate type before
   checking `alias_type_refs`.
   Index-result inference now detects foreign/qualified indexable receivers from
   `TypeRef` head/kind metadata instead of rendering the receiver type to search
   for namespace separators.
   Swizzle result matching and native generic inference conflict checks now use
   `type_ref_equivalent` for semantic type equality; rendering remains only for
   diagnostics and template substitution text boundaries.
   Binary and comparison operator validation now checks unknown/auto,
   foreign-type sameness, generic-parameter sameness, string, numeric, and
   integer predicates from `TypeRef` helpers instead of rendered type strings.
   TypeRef assignment compatibility helpers for reference binding, const pointer
   binding, pointer-to-reference values, and const unwrapping now compare
   unwrapped `TypeRef` nodes directly; the text versions remain only at explicit
   legacy/native text boundaries.
   Structural type compatibility now ignores C `struct`/`class`/`union`/`enum`
   tags through recursive `TypeRef` comparison instead of rendering whole types
   and stripping tag text.
   Unary, boolean, contextual numeric, comparison, and binary expression checks
   now use `TypeRef` predicates for bool/auto/integer/numeric decisions and
   render type text only inside diagnostic branches.
   Loop iterable binding checks now also keep the success path structured and
   render binding/element type text only when reporting a mismatch.
   Generic method expected-return inference now accepts an expected-return
   `TypeRef` directly instead of reparsing rendered return text.
   Function call, enum payload, return, assignment, and array-literal checks now
   render expected/got type text only on diagnostic paths.
   Native value type inference now reads `native_value_type_refs` directly.
   The `native_values` string map remains only as a display/native-boundary
   mirror populated from structured `TypeRef` data during symbol collection.
   C++ associated-type assignment compatibility, such as iterator/value_type
   aliases from imported native templates, now checks structured `TypeRef`
   names before falling back to native spelling compatibility.
   The final type-compatibility fallback now parses normalized native spellings
   once into `TypeRef`s and reuses the structured pointer/reference/const,
   function-pointer, internal-template, and associated-type helpers. The
   duplicate string overloads for those checks have been deleted.
   Native overload template matching no longer retries failed argument binding
   through a rendered-string template binder. The structured binder now handles
   native C++ pack-expansion artifacts inside template types, and the old
   string binder API has been deleted.
   `substitute_type_ref` now has a structured `TypeRef` substitution overload.
   Receiver generic substitution for fields, methods, inherited methods, and
   inferred generic method signatures uses parsed receiver template arguments
   directly instead of rendering them to strings and reparsing them.
   Generic function and class instantiation now also build substitution maps
   with `TypeRef` values directly, removing another render/reparse step from
   native Dudu generics.
   Module import aliasing now also builds qualified/selective type
   substitutions as `TypeRef` maps. Imported aliases, class shapes, constants,
   and function signatures preserve structured type data through import
   rewriting and render only their native mirror fields after substitution.
   Explicit method template arguments now parse into `TypeRef`s before method
   signature instantiation, and the old string receiver-template substitution
   helper has been deleted.
   Bound native template substitution now uses structured `TypeRef`
   substitution for non-pack signatures with ordinary bindings. Native C++
   variadic packs and decay artifacts remain at the native spelling boundary
   because those are foreign signature encodings, not Dudu AST syntax.
   Match wrapper recognition exposes only the parsed `TypeRef` API; the dead
   string reparsing overload has been removed. `cpp(...)` escape inference now
   keeps member-path, index, pointer-cast, and unary address/deref helper
   results as `TypeRef` nodes until the explicit escape-boundary render.
   Enum declaration lookup, foreign C++ type detection, and pointer type
   lowering no longer expose unused raw-string overloads that only parsed text
   and delegated to structured `TypeRef` implementations.
   Native function display and merge-key code now renders directly from
   `TypeRef` parameter/return helpers, and the public native/function return
   string helper APIs have been removed.
   The dead rendered-string alias resolver has been deleted; alias resolution
   now goes through `resolve_alias_ref` and structured `TypeRef` metadata.
   Pointer-cast C++ emission now parses candidate pointee types once into
   `TypeRef` nodes and reuses those nodes for lowering, rather than rendering
   a type-like callee and reparsing it through separate predicate/lowering
   helpers.
   Static member-call checks and swizzle result inference now build named
   `TypeRef` nodes directly for known class names instead of reparsing class
   name strings as type syntax.
   Template-call unknown-type diagnostics now build a named `TypeRef` directly
   for plain callees instead of reparsing the callee string. The remaining
   string spelling helper is named `known_type_spelling` and is confined to
   explicit `cpp(...)` escape inference.
   `named_type_ref` now preserves qualified-name shape for dotted or `::`
   names, and template-constructor recognition accepts parsed `TypeRef`
   metadata instead of reparsing a rendered callee string.
   Pointer-cast calls such as `*struct State(value)` now carry the parsed
   pointee `TypeRef` on the expression node. Semantic analysis reads that AST
   field and rejects malformed internal pointer-cast nodes instead of reparsing
   the rendered callee string.
   C++ emission-side call type probing now builds named `TypeRef` metadata for
   type-like constructor calls instead of reparsing the callee string.
   Literal, container-literal, all-caps constant, range-loop binding, and
   emission-side literal inference now construct built-in `TypeRef` nodes
   directly instead of invoking the type-text parser for names like `i32`,
   `bool`, or `auto`.
   Member-path helpers, LSP local-context binding, function-signature defaults,
   module import aliases, native fallback signatures, and built-in method
   helpers now also construct built-in `TypeRef` nodes directly. Remaining
   built-in parser calls are confined to the type parser and native header
   parser.
   Function-type parsing and native macro fallback metadata now also construct
   `void` and `auto` `TypeRef` nodes directly. The broad built-in parser-call
   scan is now down to explicit parser/native type parsing and `cpp(...)`
   escape-boundary text.
   Assignment compatibility now preserves existing `TypeRef` nodes when C++
   artifact normalization is a no-op, and only parses normalized spellings at
   the native compatibility boundary. The generic `TypeRef` assignment path no
   longer reparses normalized expected/got strings before structural checks.
   Member-path class references and simple literal compatibility now build
   named `TypeRef` nodes directly rather than parsing known type-name strings.
   Class-emission pseudo-locals and built-in method return signatures now also
   build named `TypeRef` nodes directly for known type names.
   Structural compatibility checks for known names such as `void` and `auto`
   now compare named `TypeRef` nodes directly instead of parsing those strings.
   Normal expression sema now consumes a typed `cpp(...)` inference API. The
   string-to-type parse for `cpp(...)` expression inference is confined inside
   the explicit escape-boundary implementation instead of happening in the
   ordinary expression sema path.
   Parsed method receiver declarations and synthetic `build.*` value symbols now
   construct `TypeRef` nodes directly instead of rendering simple type names and
   reparsing them during parser/sema setup.
   Dudu module import alias synthesis now constructs qualified `TypeRef` nodes
   directly for `module.Type` and aliased selective imports instead of routing
   module metadata through the text type parser.
   Pointer-style type calls in the expression token parser now parse their
   callee type from the original token span, removing another normal expression
   parser render-and-reparse path.
   Angle-bracket template arguments in the type-token parser now parse from
   token subspans instead of reconstructing each argument as text and sending it
   back through `parse_type_text`.
   `from module import Type as Alias` alias materialization now stores direct
   named `TypeRef` metadata for imported Dudu enums/classes instead of reparsing
   the imported symbol name during module loading.
   Native template substitution now converts simple explicit template bindings
   directly into `TypeRef` nodes and uses typed substitution for structured
   signatures, leaving text parsing only for complex native fallback spellings.
