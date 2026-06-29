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

Current direction notes:

- Concurrency stays ordinary until proven otherwise: prefer blocking functions,
  threads, queues, `select`/`poll`/platform event APIs, and explicit state
  machines over core-language `async`/`await`. See
  [Project Goals](goals.md#concurrency-philosophy).
- Value `match` lowering should stay lean. All-return value matches now lower to
  direct ordered returns without a generated `matched` boolean; non-returning
  value matches lower to ordered `else if`/`else` chains. See
  [Sum Types Plan](sum-types-plan.md).
- Compiler and LSP scratch artifacts must stay out of source directories. Native
  header scanner scratch files belong in temp/cache/build locations, not beside
  `.dd` files.

If a planned feature cannot meet that bar yet, document the missing prerequisite
and implement the prerequisite first.

The current implementation grew from a prototype, so some compatibility strings
and backend shortcuts still exist. The goal is not to freeze that shape. The
goal is to keep working features moving while steadily separating the compiler
into boring, traceable phases. New work should push responsibilities into the
proper phase instead of adding more cross-phase patching.

## Prototype Cruft And Style Pass

Fast compiler work leaves sediment: one-line wrapper functions, duplicated
helpers, vague fallback names, overgrown files, compatibility branches for
syntax that should never ship, and small local patches that were reasonable for
one milestone but are silly once the architecture catches up. Dudu needs an
explicit cleanup pass for that work, not just feature implementation.

Required cleanup behavior:

- delete one-line wrappers that do not encode a real abstraction or boundary
- delete vacuous helpers such as generic functions that always return a
  constant value, wrappers that only forward arguments unchanged, and helpers
  whose only purpose was to satisfy a temporary call shape
- merge duplicate helpers into one clearly owned implementation
- split files that violate the project style rules or mix unrelated compiler
  phases
- remove stale compatibility paths for unreleased syntax instead of preserving
  them for nonexistent users
- remove "make it compile" branches once the real parser, module resolver,
  type checker, or native-boundary model owns the behavior
- rename helpers so `fallback`, `legacy`, `compat`, and `raw` only appear at
  intentional native-boundary or migration-boundary APIs
- move reusable logic out of ad hoc local lambdas when it has become a compiler
  concept
- delete dead fixtures, unused options, and workaround examples once the real
  language feature exists
- prefer a hard diagnostic over silently accepting a malformed internal state
  that would make later phases guess
- keep cleanup commits behavior-preserving unless the cleanup exposes a bug;
  when it exposes a bug, add the regression fixture in the same slice

This pass should run after each major architecture slice and before claiming a
phase complete. The goal is not cosmetic churn. The goal is to keep Dudu from
turning into a compiler made of successful experiments glued together forever.

## Proper AST And Compiler Pipeline

Destringing sequence: [Destringing Goals](destringing-goals.md).

This is the next architecture priority. Execute the meta goal in
[Destringing Goals](destringing-goals.md) before continuing the rest of this
roadmap. Dudu must stop treating raw statement text as semantic data.

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

## Website And Public Face

Website plan: [Website Plan](website-plan.md).

Dudu should have a GitHub Pages site for `dudulang.org`. The homepage should
visually mimic the recognizable `mojolang.org` front page as a brown/poo-themed
joke: same broad hero/nav/quick-link/feature-card rhythm, but Dudu branding and
original implementation. Everything beyond that front-page gag, including
install, docs, examples, roadmap, and technical content, must be Dudu-specific
and technically honest.

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
diagnostic message boundary. A partial-branch-return fixture guards against
flagging code after a non-exhaustive `if` as unreachable, and a multiline
return-continuation fixture guards against treating continued expression lines
as unreachable statements. Remaining work is to keep splitting mixed
lint/code-action logic, move any residual edit construction onto concrete
AST/token ranges, and add more false-positive fixtures for realistic
project/module cases. Import organization in the formatter and LSP code actions
now shares structured leading-import block organization and parsed
`ImportDecl` rendering instead of carrying duplicate import sorting/string
builders, and generic document line helpers live in LSP support instead of
individual action files. AST-backed unused-local and shadowing checks have also
been split into a focused scope-lint pass, leaving the aggregate AST lint entry
point responsible for orchestration instead of owning every lint rule. Lint
diagnostics for removable unreachable statements and unused locals now carry a
structured `data.fixRange` computed by the AST lint pass, and the corresponding
LSP quick fixes consume that range instead of reconstructing deletion edits from
document line text.
AST-backed suspicious narrowing-cast checks now also live in a focused lint
pass with typed local state. Raw `cpp(...)` escape-hatch warnings also live in
a focused lint pass, so the aggregate AST lint entry point is now only a
dispatcher. A
multi-module lint fixture guards against reporting a
dependency module's unused local in the entry document's diagnostics.

## Resolved Critical Module Import Blocker

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
constants, and functions. Python-shaped namespace boundaries now reject
transitive dependency leakage and private selective-import leakage; modules
only expose their own declarations through qualified imports unless a facade
defines an explicit public wrapper. The old merged-output compatibility path
still exists under low-level `duc` emission/build commands, but Dudu project
builds use generated CMake so modules compile through per-module artifacts.
Qualified Dudu module imports such as
`import camera as cam` and
`import renderer.camera` now bind `cam.Type`, `cam.function`,
`renderer.camera.Type`, and `renderer.camera.function` through non-emitted
module metadata. Distinct modules that declare the same unqualified Dudu type
or function name now preserve their declaration origin in the AST, which gives
the namespace backend the information it needs. Source-tree loads also preserve
the ordered per-file module units alongside the compatibility merged view, so
the next sema/codegen work can operate module-by-module. Each module unit now
also carries resolved dependency metadata for Dudu imports, including the source
import spelling, canonical resolved module path, and resolved source file.
Selective `from module import Name` bindings now reject collisions with local
declarations or earlier selective imports at import materialization time, with
a source diagnostic that tells the user to add `as` for a unique local name.
Parser-level duplicate direct import checks use the same aliasing guidance.
Cycle diagnostics now report the module graph path that closes the cycle, for
example `a -> b -> a`, at the import statement that introduced the cycle.
Same-name declarations now also carry stable generated C++ names derived from
their owning module. The per-module artifact emitter now uses those
generated names for same-module declarations and qualified imported module
references, including `import module as alias`, `import module.path`, and
selective `from module import Name` forms. `duc emit-modules` now analyzes each module unit in its
own semantic scope, and per-unit imports materialize qualified/selective
symbols without pulling dependency declarations into the current module. The
generated CMake backend has a regression fixture with two modules that both
declare `Box`, `make`, and `score`; those compile as separate generated C++
artifacts with distinct generated names. A separate fixture covers same-named
functions without same-named types, proving both the generated-CMake success
path and the merged-output rejection path. A CMake-backend negative fixture now
rejects transitive import leakage, so importing a facade module does not let the
importer use the facade's private dependencies by accident. Qualified Dudu
module prefixes are also tracked separately from opaque native C++ prefixes, so
an imported module's private selective imports, such as
`facade.hidden_answer`, are rejected by Dudu sema instead of leaking through to
generated C++ as unknown native calls. The merged-output compatibility path now
rejects cross-module declaration-name collisions with a diagnostic that points
users at `dudu build` or `duc emit-modules` instead of surfacing generic
duplicate-declaration fallout. The same guard also applies to explicit merged
`--emit-cpp`/header output, even when the project manifest uses the CMake
backend. A focused frontend regression proves the module tree can analyze
successfully while the merged-output compatibility backend rejects the same
named declarations with that backend guidance.

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

Status note: generated CMake backend invocations now take a per-build-root lock
so concurrent `dudu build <target>` runs in one project serialize instead of
clobbering the shared generated `CMakeLists.txt` and producing misleading
CMake target errors.

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

Primary plans: [Native Header Awareness Plan](header-awareness-plan.md) and
[Native Identity Plan](native-identity-plan.md).

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
- canonical native identities instead of raw C/C++ spelling equality
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
   Out-of-line method attachment now compares the receiver's structured
   `TypeRef` head name directly; the old `function_receiver_type_text` helper
   that rendered the receiver type only to compare it with class names has been
   deleted and guarded.
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
   Receiver unwrapping now exposes the structured `unwrap_receiver_type_ref`
   path for semantic work; the string-producing helper is explicitly named
   `receiver_class_name` and is used only where a class lookup key or
   diagnostic class label is required.
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
   Direct unaliased native symbols from C/C++ headers now participate in
   current-document reference lookup once the native scanner proves the symbol
   exists in the document's imports, covering direct native function calls.
   Dudu-owned declaration and unique-reference scope checks now use Dudu-only
   document symbols, keeping ordinary references off the native-header scanner
   path unless the selected symbol is explicitly native.
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
   Type-token parsing now fills semantic `TypeRef` names and numeric
   template values from token spans directly instead of copying from the raw
   `TypeRef.text` source spelling mirror.
   Common `TypeRef` head-name queries now derive pointer, reference, wrapper,
   function, value, template, and fixed-array heads from structured fields,
   keeping raw text only for explicit unknown/native-boundary spellings.
   `substitute_type_ref_text` now rejects malformed structured pointer,
   reference, wrapper, and fixed-array nodes instead of rendering stale raw
   `TypeRef.text` as a compatibility fallback.
   Literal parsing/codegen now treats bool, numeric, and string literal values
   as parsed `Expr::value` data; C++ string literal emission renders from that
   parsed value instead of forwarding the source spelling mirror.
   Expression display for diagnostics now renders ordinary literals from
   parsed values and reports malformed structured binary/member/index/dict
   entry/named-argument/slice nodes explicitly instead of falling back to raw
   `Expr::text`.
   Sema and C++ emission unsupported-expression diagnostics now call the shared
   structured expression display helper instead of reading raw expression text
   directly at each diagnostic site.
   Unused top-level substring scanners for assignment, member-dot, binary
   expression, and function-arrow type parsing have been deleted from the parse
   utility API. Statement assignment detection stays in the token parser, and
   the AST migration guard rejects reintroducing those old string scanner
   helpers.
   The remaining top-level comma/colon/member-path string splitting used by
   `cpp(...)` inference has been renamed behind explicit `cpp_escape` helper
   names, so normal sema/codegen cannot accidentally depend on generic
   string-scanner APIs.
   Index sema helper parameters now use receiver-type terminology for structured
   `TypeRef` values instead of `raw_type` names, and the guard rejects moving
   those misleading names back into the normal index sema path.
   Bound native signature substitution now keeps its render/reparse fallback
   behind explicitly named native-spelling helpers. Structured `TypeRef`
   substitution remains the normal path, while messy C++ template artifacts
   such as pack spellings and `__decay_and_strip` are quarantined at the native
   boundary and guarded against generic helper names returning.
   Decorator argument helper names now say `display` instead of `text`; they
   render already parsed decorator expression nodes for C++ emission rather
   than scanning decorator source text, and the migration guard rejects the old
   text-shaped API names.
   Fixed-array `array[T][shape]` nodes now carry shape dimensions as structured
   child `TypeRef` nodes after the storage type. Normal sema, slicing, generic
   non-type parameter discovery, structural assignment, and C++ array lowering
   consume those shape children instead of splitting the comma-shaped value
   mirror. Fixed-array equivalence also compares the structured shape children
   instead of the value mirror, so whitespace in the mirror cannot change type
   identity. Fixed-array `type_ref_same_shape` follows the same structured
   shape rule. The native scan cache version was bumped so stale one-child
   fixed array nodes are regenerated, and the guard rejects the removed
   `explicit_array_shape_text` helper.
   Native overload and constructor mismatch helpers now use `display` names for
   diagnostic-only rendering, and the migration guard rejects the old `text`
   helper names so normal matching paths do not drift back toward string-shaped
   semantics.
   Generic body specialization now substitutes generic parameters only in
   `Name` expressions, so constructor field names, member labels, and other
   expression metadata are not accidentally rewritten just because they share a
   spelling with a generic parameter.
   Body and expression semantic diagnostics now name rendered types as
   `display` values, and an unused `shape_text` helper was deleted and guarded
   so normal sema code does not regain stale text-shaped helper names.
   Assignment, function-call, enum-constructor, and member-path diagnostics now
   follow the same `display` naming, with a guard against reintroducing
   receiver-type `text` variables in normal semantic code.
   Pointer-cast parsing now stores the target as a parsed `TypeRef` on a normal
   call expression with a `*` operation marker; it no longer manufactures
   fake callee strings such as `*list[i32]` from source spans.
   The remaining internal AST callee renderer is named as display-only, and the
   migration guard rejects the old `call_callee_text` helper name.
   Native overload and assignment compatibility mismatch helpers now use
   display-only naming at the diagnostic edge, and the guard rejects the old
   text-shaped helper names.
   Generic function body type substitution has been split out of
   `sema_body.cpp` into a focused helper module, keeping body checking under
   the file-size budget and making the remaining expression-name substitution
   surface explicit.
   Parser token-span rendering is now named `token_source_spelling` and used
   only for parser diagnostics and explicit `cpp(...)` escape bodies; the guard
   rejects the old generic `source_text_for_tokens` API name.

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
   `@property` are rejected with explicit OOP-surface diagnostics. Regression
   fixtures cover stale Python dunder lifecycle/operator names, stale
   static/class/property decorators, and explicit member visibility keywords so
   old prototype spellings do not silently come back.

4. Arrays, Matrices, Tensors, And Slicing

   Primary plan: [Arrays, Matrix, Tensor, And Slicing Plan](arrays-indexing-plan.md).

   Migrate current fixed-array syntax to canonical `array[T][shape]`, add
   initializer-based shape inference for `array[T] = literal`, then add
   shape-aware fixed arrays, matrix/tensor indexing, swizzling, and explicit
   view/copy slicing semantics so numeric, graphics, and image code feels
   natural without hidden allocation.

   Status: explicit `array[T][N]`, `array[T][M, N]`, and higher-rank
   `array[T][shape]` compile through structured `TypeRef` lowering for normal
   Dudu source, explicit-shaped literal initializers are checked,
   `array[T] = literal` infers fixed shapes for rectangular matrix and volume
   literals, and comma indexing such as `mat[row, col]` lowers for Dudu-native
   fixed arrays. Partial array indexing is type-aware and over-indexing is
   diagnosed.
   Fixed arrays work with `len(values)` and `&values[0]` for native
   pointer/count handoff.
   One-dimensional fixed-array `start:end`, `:end`, `start:`, and `:` slices
   produce `span[T]` views. Fixed multidimensional arrays support trailing row
   views such as `mat[row, :]`, and contiguous leading-axis slab ranges such as
   `mat[start:end, :]` and `volume[start:end, :, :]` produce `span[T]` views
   over the selected storage. Matrix column slices such as `mat[:, col]`
   produce `strided_span[T]` views so non-contiguous views are explicit,
   including generic non-type extents and member-backed arrays.
   Three-dimensional channel slices such as `image[:, :, c]` also produce
   `strided_span[T]` views over interleaved channel data, including generic
   non-type extents and member-backed arrays. Trailing-dimension range slices
   such as `image[y, x, 0:3]` produce contiguous `span[T]` views, including
   generic non-type extents and member-backed arrays. Two-dimensional
   patch rectangles such as `mat[y0:y1, x0:x1]` produce `strided_span2[T]`
   views with explicit row stride, and `strided_span2[T]` views are iterable in
   row-major order. Existing `strided_span2[T]` patch views can also be
   resliced into full patch, row, column, row-range, column-range, and subpatch
   views, so matrix patches can be reused instead of immediately copied.
   Full-rank fixed-array slices such as `mat[:, :]` and
   `image[:, :, :]` produce contiguous `span[T]` views over the whole backing
   storage, including generic non-type extents such as
   `array[T][Rows, Cols]` and member-backed fixed arrays such as
   `self.items[:, :]`.
   Dudu-native `@operator("[]")` read hooks and `@operator("[]=")` indexed
   assignment hooks work for library-style tensor wrappers, including member
   receiver assignments such as `box.tensor[i] = value`, and indexed member
   paths such as `self.values[i]` type-check. One-dimensional fixed-array step
   slices such as `values[start:end:step]` produce `strided_span[T]` views.
   This is enough to play with fixed arrays, row/column/full-storage views,
   channel views, basic step slices, and library indexing hooks, but it is not
   full NumPy-style indexing yet. Non-contiguous multidimensional patch
   rectangles beyond rank-2, advanced gather/scatter indexing, broadcasting
   rules, shape metadata for library tensors, and GPU-backed tensor-library
   hooks remain.
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
   `i32` function. Non-type generic value parameters now cover fixed-array
   extents such as `class SmallVec[T, N]` with `items: array[T][N]`, lowering
   to `template <typename T, size_t N>` and rejecting value parameters used as
   types. Free-function generic calls re-check instantiated bodies with
   concrete type arguments, so unsupported operator use such as `add[Player]`
   fails in Dudu sema instead of surfacing only as C++ compiler output. Richer
   instantiated diagnostics for generic methods and classes remain.
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
   exhaustive coverage, later cases after an unguarded wildcard, and guarded
   cases that appear after an earlier unguarded case for the same variant.
   Richer subsumption only matters if the pattern language grows nested
   alternatives or range-like patterns.

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

   Identity plan: [Native Identity Plan](native-identity-plan.md).

   Improve overload diagnostics, const/reference modeling, explicit C++
   template calls, template-heavy library behavior, header cache invalidation,
   cache cleanup, scanner failure UX, and canonical native identity modeling.
   Native spelling is display/emission metadata; semantic identity should come
   from canonical native declaration metadata when available.

   Status: overload diagnostics, explicit native C++ template function calls
   including multi-argument calls, local-header and transitive included-header
   cache invalidation, cache cleanup, broken-Clang diagnostics, missing-header
   diagnostics, and common const pointer/reference lowering are implemented.
   Explicit native template
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
   The broader standard-library interop fixture covers `std.optional`,
   `std.array`, `std.span`, `std.map`, `std.unordered_set`, `std.function`,
   smart pointers, and mutex guard construction; it remains fixture-level
   coverage rather than fast-loop codegen because scanning that many standard
   headers is intentionally heavier.
   Native overload failure diagnostics list argument types, candidate
   signatures, and per-candidate arity or all mismatched fixed-parameter
   reasons. Native overload matching accepts Dudu string literals for native
   `string_view`/`basic_string_view` parameters, matching normal C++ call
   ergonomics for APIs such as `fmt.runtime(...)`. `None` is accepted for
   nullable native `cstr` parameters, matching common C APIs such as
   `XOpenDisplay(nullptr)`. Dear ImGui core namespace APIs work through
   ordinary unaliased C++ imports when the project provides the normal include
   metadata. Boost filesystem path construction, boolean member calls, and
   string-returning overloads work without wrappers; native C++ method
   templates preserve scanned template parameter metadata so unresolved method
   templates do not mask concrete overloads.
   Broader template-heavy library behavior remains the main hardening area.

10. Real Library Stress Tests

   Compatibility matrix: [Native Compatibility Matrix](native-compatibility-matrix.md).

   Keep proving SDL3, ImGui, raylib, glm, sqlite, POSIX, OpenCL, Vulkan, GLFW,
   and FFmpeg style APIs with normal imports and minimal wrapper code.

   Add and maintain a C/C++ ecosystem compatibility matrix. This should track
   common libraries by domain, with a short status for each library:

   - import/header scan
   - function calls
   - structs/classes/fields
   - macros/constants
   - overloads/templates/operators
   - build/link through `dudu build` and generated CMake
   - tiny real executable example
   - wrapper header needed, and why

   Start with roughly the top 100 useful C/C++ libraries and APIs across:

   - C/POSIX/libc/pthreads and the C++ standard library
   - SDL3, raylib, GLFW, Dear ImGui, OpenGL, Vulkan
   - glm, Eigen, BLAS/LAPACK-style numeric libraries
   - sqlite, zlib, libpng, stb, curl
   - fmt, spdlog, Boost subsets
   - OpenCV, FFmpeg
   - OpenCL, CUDA/CUBLAS when hardware/tooling is available
   - common platform APIs used by games, tools, embedded, and systems code

   Failures in this matrix should become compiler/language issues when the
   library is doing normal C/C++ work. Wrapper headers are acceptable only when
   the library relies on macro-generated declarations, token-pasting,
   partial-syntax macros, or platform setup that C/C++ users also normally hide
   behind an adapter.

   Current optional probes pass for glm, Eigen, OpenBLAS, OpenCV, sqlite, zlib,
   curl, OpenSSL, libevent, libxml2, Expat, Cairo, FreeType, libpng, libjpeg,
   Lua, libuuid, stb, fmt, Boost filesystem, threading, POSIX mmap, POSIX
   pthread, liblzma, raylib, SDL3, GLFW, Dear ImGui, X11, Wayland, OpenCL,
   Vulkan, and FFmpeg on this machine. `spdlog` remains an intentional
   heavy/manual probe behind `DUDU_PROBE_HEAVY=1`; the manual spdlog smoke
   passes locally but is too slow for the default optional sweep. Optional
   dev-only dependencies can be installed into the ignored
   `third_party/install` prefix with `scripts/setup_dev_deps.sh`; the main Dudu
   build does not require them.

   POSIX pthread coverage now imports `pthread.h` directly and exercises
   `pthread_create` with a Dudu function pointer callback plus native mutex
   operations, without the old helper wrapper header.
   C imports emit C-linkage include blocks for headers that need the normal C++
   `extern "C"` wrapper pattern, such as FFmpeg's `libavcodec/packet.h`.
   `import cxx` covers C++-aware C headers that own their own linkage blocks
   but expose C-style globals; libxml2 imports directly through that path.
   libjpeg coverage now touches `jpeg_compress_struct.err`, which comes from
   the `jpeg_common_fields` preprocessor member macro. Native header scanning
   first tries the target header alone, then retries with a minimal C prelude
   for context-dependent C headers and filters retry declarations back to the
   target header so aliases do not absorb unrelated standard-library symbols.
   Aliased native calls must now resolve through real scanned function
   signatures; the old broad `alias.anything` native-call fallback was removed
   because it let unrelated transitive headers appear as members of the alias.
   libuuid coverage now declares `uuid.uuid_t` locals directly and passes them
   to native functions whose parameters scan as `*u8` or `*const[u8]`, matching
   ordinary C array-to-pointer handoff for typedefed fixed arrays. Mutable
   `char *out` buffers scan as `*char` rather than input-only `cstr`, so
   user-owned output buffers work through normal array handoff.

   This matrix is not part of the always-on fast loop. Keep fast compiler
   validation small, and run the real-library/example matrix periodically, when
   touching native interop or build-driver behavior, and before version bumps.
   `duduplayground`, `raymarch-dd`, `dudu-webserver`, and similar external
   repos are dogfood inputs, not required public fixtures; curated,
   reproducible examples should live in the Dudu repo when they become
   official compatibility checks. `dudu-webserver` is currently a useful
   POSIX/C++ standard-library dogfood repo because it stresses multi-file Dudu
   modules, generated CMake builds, repeated native imports, sockets, `poll`,
   time APIs, environment reads, and C++ string interop without wrapper
   headers.

11. Compiler Throughput And Build Performance

   Treat compiler speed as a first-class language quality metric. Dudu should
   track its own frontend throughput and native build overhead separately from
   the speed of generated programs.

   Add a compiler benchmark suite that records:

   - lexer/parser/sema/codegen time in lines per second and files per second
   - native header scan time, cache hit time, and cache miss time
   - generated C++ emission time
   - generated C++ compile and link time
   - clean build, no-op rebuild, and one-file-changed rebuild time
   - LSP parse/diagnostic latency
   - peak memory for large source trees and native-heavy header scans

   Cover several project shapes:

   - tiny single-file script-style program
   - medium multi-module Dudu project
   - large synthetic Dudu source corpora for frontend throughput, split across
     diverse generated shapes instead of only one easy case:
     many functions, many classes/methods, expression-heavy bodies, and
     multi-module import graphs
   - native-heavy imports such as standard library, SDL3, raylib, glm, sqlite,
     and POSIX headers
   - generated CMake backend projects

   `dudu bench compiler` or a dedicated `scripts/bench_compiler.sh` should emit
   machine-readable JSON or CSV plus a readable summary. Establish local
   baselines first, then add thresholds once the compiler architecture is stable
   enough for numbers to mean something.

   Keep compiler-speed benchmarks out of the default fast loop. Fast validation
   should catch correctness regressions quickly; throughput benchmarks should be
   explicit so a slow or hung benchmark never blocks normal development. Do not
   trade compiler architecture, diagnostics, or import correctness for a
   premature lines-per-second win.

   Status: `scripts/bench_compiler.sh` exists as an explicit developer
   benchmark outside the fast correctness loop, and `dudu bench compiler`
   dispatches to it when run from a checkout containing that script. It emits
   CSV plus a readable summary and currently measures representative frontend
   check, C++ emission, native header cold/cache check,
   generated-CMake module build, generated-CMake no-op rebuild, and
   generated-CMake one-Dudu-file-changed rebuild cases. It also measures a
   lightweight LSP parse/diagnostic/document-symbol roundtrip through
   `duc_lsp_diagnostics`, so editor latency is visible outside the full LSP
   smoke suite. A generated synthetic multi-module corpus under
   `build/bench_compiler` gives frontend throughput a larger parse/sema input
   than the tiny correctness fixtures. The benchmark also generates diverse
   scalable frontend-throughput corpora through `--line-scales` and code shapes
   such as functions, classes, expressions, modules, calls, control flow,
   arrays, indexing/slicing/swizzling, generics, pattern matching/sum types,
   and operator overloads, defaulting to
   `1000,5000,10000` lines and allowing explicit stress runs such as
   `--line-scales 10000,50000,100000,200000,500000,1000000`. The generated
   shapes are selectable with
   `--shapes functions,classes,expressions,modules,calls,control,arrays,indexing,generics,matches,operators,stdlib,mixed`
   so profiling can distinguish slow declaration lookup, class/member handling,
   expression parsing/sema, import/module behavior, nested calls, control-flow
   blocks, array basics, matrix/tensor-like indexing and slices, generic
   instantiation, match lowering for enums/sum types, operator overload
   resolution, standard-library native interop, and mixed project-shaped code
   that combines imports, classes, methods, generics, arrays, loops, calls, and
   arithmetic in one generated corpus. The `stdlib` shape is intentionally explicit rather than default
   because it pulls real C++ standard headers through native scanning and is
   much slower than pure Dudu frontend shapes; a local 1k Release run measured
   about 2.6s and 101MB RSS cold, then about 114ms and 47MB RSS on cached
   samples. Use it when validating native interop throughput instead of every
   routine compiler-speed loop. Cached native scan loading then stopped reading
   raw `.ast` and `.macros` dumps before trying the compact `.scan` cache.
   Direct timings moved the seven-header stdlib cache section from about 66ms
   to about 45ms, and a three-sample harness run moved cached samples to about
   105ms and 36MB RSS while leaving pure-Dudu generated shapes within normal
   noise. Standard-library imports then stopped baking the importing source
   directory into the native scanner flags when no matching local header
   exists, so system-style headers can share cache entries across generated
   projects. In a 1k/5k stdlib scale run, `stdlib_5000` stopped paying a
   second roughly 2.7s cold scan after `stdlib_1000` and ran around 215ms from
   the shared standard-header cache.
   Compiler-speed claims must be checked against multiple generated shapes and
   at least one real dogfood project when practical, because one benchmark can
   hide that only a specific compilation feature is slow. Generated corpora
   should deliberately stress different language surfaces, not just line count:
   declarations, imports, calls, control flow, arrays/indexing, generics,
   classes, native interop, diagnostics, and mixed realistic project layouts.
   Add both isolated stress shapes and combination cases that cross features
   such as templates plus overloads plus native imports, because a compiler can
   look fast on every single-feature corpus while one real feature interaction
   dominates user-visible compile time. Every compiler-speed optimization must
   be tested against a diverse shape set selected for the suspected hot path,
   not only the benchmark that first exposed the slowdown.
   Reject a speed patch if it only improves one generated shape while
   regressing other representative shapes; that is a local trick, not a
   compiler throughput win.
   Benchmarks can select
   `--build-type Debug`, `Release`, or `RelWithDebInfo`; Debug measures
   inner-loop compiler development pain, while Release measures shipped-tool
   speed. Summary output reports lines per second in addition to elapsed time
   and peak RSS. A local one-sample
   baseline on 2026-06-28 measured the original many-functions shape at about
   13.3s and 318MB RSS for roughly 10k generated Dudu lines, which is too slow
   for the language goal and should trigger profiling before larger 50k+ runs
   become useful. A later 5k one-sample diverse run showed classes/modules
   around 0.5-0.6s, while many-functions and expression-heavy cases were around
   3.5-3.6s, so optimization work should prioritize those paths before import
   mechanics. The first measured optimization stopped cloning the full module
   symbol table for every non-generic function/method declaration and body
   check; locally this dropped the 5k many-functions case from about 3.5s to
   about 0.5s while leaving expression-heavy code as the next clear hotspot. A
   second measured optimization stopped low-level `duc check` from loading the
   full module tree for single-file sources without Dudu module imports. Native
   C/C++ imports still stay on the single-file path and native-merge into that
   root module. Locally this dropped the 5k many-functions case further to
   about 0.22s and the 5k expression-heavy case to about 2.0s with much lower
   RSS. A parser fast path now avoids copying expression/type token spans when
   they contain no layout tokens; this keeps multiline continuation semantics
   unchanged while dropping the local 5k expression-heavy parse/load phase from
   about 0.87s to about 0.76s. Source locations now store file names as
   strings instead of full `std::filesystem::path` objects, avoiding repeated
   path-internal allocations in tokens, ranges, and AST nodes. Locally this
   dropped the 10k expression-heavy Release check from about 0.60s and 458MB
   RSS to about 0.25s and 227MB RSS with direct `duc --timings`; the benchmark
   harness measured about 0.29s and 249MB RSS including its process/RSS wrapper
   overhead. A local one-sample 10k Release run of the expanded shapes measured
   calls, control flow, arrays, and generics around 0.05-0.07s each, so
   expression-heavy bodies remain the main frontend throughput outlier. The
   parser now avoids allocating tuple-item storage for expressions that do not
   actually contain a comma and pre-reserves lexer token storage from source
   size. A naive statement-block reserve based on the remaining token span was
   tried and rejected because nested control-flow blocks over-reserved and made
   control-heavy benchmarks worse. Do not retry that shape without a depth-aware
   statement count. Lexer token reservation now uses a less stingy source-size
   estimate; local direct 10k expression timings showed a lower load phase, but
   the broader harness treated this as a small allocation-churn tweak rather
   than a major throughput win. `SourceLocation` filenames now use interned
   immutable filename storage instead of copying the source path into every
   token and AST range. On a local one-sample 10k Release run, expression-heavy
   checks dropped from about 283ms and 244MB RSS to about 256ms and 122MB RSS,
   modules from about 80ms and 131MB RSS to about 52ms and 80MB RSS, and
   call/control-heavy shapes also dropped into the low-to-mid 40ms range. The
   statement parser now finds top-level colons and assignment operators in one
   pass over generic statements instead of scanning the same token span twice;
   direct 10k expression timings showed lower load time, while the broad
   one-sample shape sweep was neutral-to-positive. An O(1) `join_tokens` range
   shortcut using only first and last token locations was tried and rejected
   because repeated direct/harness timings were neutral-to-negative after noise
   settled. A thread-local "last filename" cache inside the source filename
   interner was also tried after callgrind showed `std::set` insertion/lookup
   hot; it crashed through recursive interner entry and was reverted. Future
   filename speed work should prefer a cleaner parse-owned source-file identity
   instead of a clever interner cache. The interner backing store was then
   changed from `std::set` to `std::unordered_set`, preserving stable interned
   string storage while avoiding tree lookup cost; a local one-sample 10k
   Release shape sweep was neutral-to-positive, with expression-heavy checks
   around 254ms and modules around 49ms. A later 10k/50k Release scale run
   showed expression-heavy code is still the clear outlier but scales roughly
   linearly: about 252ms and 122MB RSS at 10k lines, about 1.2s and 533MB RSS
   at 50k lines. By comparison, many-functions reached about 0.13s at 50k
   lines. The next serious expression-throughput work should target AST storage
   shape, especially binary-expression trees that currently allocate vector
   child storage per node. A local size probe measured `Expr` at 416 bytes and
   `Stmt` at 3616 bytes, because each node currently carries many payload fields
   for every possible variant; this is acceptable for keeping the language
   moving but not for million-line compiler throughput. The first structural
   shrink moved rare expression-attached type metadata behind pointer storage,
   because it is mainly used for pointer-cast/template metadata. This dropped
   local `Expr` size from 416 to 288 bytes and `Stmt` size from 3616 to 2592
   bytes. A one-sample 10k/50k Release sweep across all generated shapes was
   positive: expression-heavy 50k dropped from about 1.2s/533MB RSS to about
   1.1s/395MB RSS, modules 50k dropped from about 188ms/352MB to about
   148ms/265MB, and the other shapes saw similar RSS reductions without an
   obvious time regression. A follow-up removed the unused `Expr::params`
   vector and its dead walker passes; no parser produced that field. This
   dropped local `Expr` size again from 288 to 264 bytes and `Stmt` from 2592
   to 2400 bytes. The same one-sample 10k/50k Release sweep stayed positive:
   expression-heavy 50k measured about 1.08s/370MB RSS, and modules 50k about
   147ms/249MB RSS. Statement-declared type metadata was then moved behind the
   same explicit helper API because only declarations, typed loop bindings, and
   typed catches carry it. This left `Expr` at 264 bytes and dropped local
   `Stmt` size from 2400 to 2272 bytes. A broad one-sample Release sweep stayed
   broadly positive on memory: expression-heavy 50k measured about
   1.10s/364MB RSS, modules 50k about 139ms/239MB RSS, functions 50k about
   113ms/115MB RSS, calls 50k about 133ms/108MB RSS, control 50k about
   143ms/119MB RSS, arrays 50k about 159ms/87MB RSS, and generics 50k about
   153ms/64MB RSS. Time differences at this size are mostly noise-level, so this
   is kept as a memory/scale win rather than claimed as a throughput win.
   Expression template type arguments were then moved behind sparse pointer
   storage, because only explicit template calls carry them. This dropped local
   `Expr` size from 264 to 256 bytes and `Stmt` from 2272 to 2208 bytes. The
   broad one-sample Release sweep stayed positive on memory: expression-heavy
   50k measured about 1.07s/355MB RSS, modules 50k about 148ms/233MB RSS,
   functions 50k about 118ms/113MB RSS, calls 50k about 130ms/105MB RSS,
   control 50k about 139ms/116MB RSS, arrays 50k about 158ms/85MB RSS, and
   generics 50k about 151ms/64MB RSS. The module time bounce is within the
   one-sample noise seen in nearby runs; this is kept for the consistent memory
   drop across diverse generated shapes. Parser joined-token spans now carry a
   `has_layout_tokens` bit, avoiding a second scan over every expression/type
   piece just to decide whether multiline layout tokens need filtering. This is
   especially relevant for expression-heavy code because it creates many parsed
   pieces. Focused `expressions_50000 --timings` runs moved the pre-sema
   parse/load interval from roughly 0.90s before this line of work to roughly
   0.62-0.63s after the layout metadata change. The one-sample broad harness
   still bounces in total time, so keep treating this as a parser-path win
   validated by focused timings plus normal fast tests, not as a final compiler
   throughput solution. Assertion custom-message expressions were then moved
   behind sparse pointer storage, because most statements do not carry assertion
   messages. This left `Expr` at 256 bytes and dropped local `Stmt` size from
   2208 to 1968 bytes. A broad one-sample 10k/50k Release sweep stayed
   positive on memory: expression-heavy 50k measured about 1.08s/343MB RSS,
   modules 50k about 145ms/212MB RSS, functions 50k about 114ms/103MB RSS,
   calls 50k about 133ms/97MB RSS, control 50k about 140ms/107MB RSS, arrays
   50k about 153ms/80MB RSS, and generics 50k about 150ms/61MB RSS. Time
   remains noise-level at this size, so keep this as a memory/scale win. Match
   guard expressions were then moved behind sparse pointer storage for the same
   reason. This left `Expr` at 256 bytes and dropped local `Stmt` size from
   1968 to 1728 bytes. A broad one-sample 10k/50k Release sweep stayed
   positive: expression-heavy 50k measured about 1.05s/331MB RSS, modules 50k
   about 137ms/193MB RSS, functions 50k about 110ms/94MB RSS, calls 50k about
   127ms/90MB RSS, control 50k about 131ms/98MB RSS, arrays 50k about
   154ms/75MB RSS, and generics 50k about 149ms/58MB RSS. Match pattern
   expressions then moved behind sparse pointer storage, keeping case matching
   structured while avoiding an embedded pattern expression on every statement.
   This left `Expr` at 256 bytes and dropped local `Stmt` size from 1728 to
   1488 bytes. A broad one-sample 10k/50k Release sweep stayed positive on
   memory: expression-heavy 50k measured about 1.06s/319MB RSS, modules 50k
   about 124ms/172MB RSS, functions 50k about 101ms/85MB RSS, calls 50k about
   124ms/82MB RSS, control 50k about 132ms/90MB RSS, arrays 50k about
   149ms/70MB RSS, and generics 50k about 144ms/56MB RSS. `for` iterable
   expressions then moved behind sparse pointer storage, because only loop
   statements carry them. This left `Expr` at 256 bytes and dropped local
   `Stmt` size from 1488 to 1248 bytes. A broad one-sample 10k/50k Release
   sweep stayed positive on memory: expression-heavy 50k measured about
   1.05s/307MB RSS, modules 50k about 116ms/152MB RSS, functions 50k about
   103ms/75MB RSS, calls 50k about 121ms/76MB RSS, control 50k about
   127ms/81MB RSS, arrays 50k about 146ms/68MB RSS, and generics 50k about
   145ms/53MB RSS. Statement condition expressions then moved behind sparse
   pointer storage. This field is less rare than match/loop-only payloads, so
   it was kept only after the broad sweep stayed positive across control-heavy
   code. It left `Expr` at 256 bytes and dropped local `Stmt` size from 1248 to
   1008 bytes. A broad one-sample 10k/50k Release sweep measured
   expression-heavy 50k around 1.04s/297MB RSS, modules 50k around
   112ms/131MB RSS, functions 50k around 95ms/66MB RSS, calls 50k around
   118ms/66MB RSS, control 50k around 122ms/75MB RSS, arrays 50k around
   140ms/62MB RSS, and generics 50k around 141ms/50MB RSS. Assignment target
   expressions then moved behind sparse pointer storage. This touches sema,
   codegen, swizzle/index assignment, and LSP target handling, so it was kept
   only after assignment-relevant generated shapes stayed positive. It left
   `Expr` at 256 bytes and dropped local `Stmt` size from 1008 to 768 bytes. A
   broad one-sample 10k/50k Release sweep measured expression-heavy 50k around
   1.04s/298MB RSS, modules 50k around 107ms/115MB RSS, functions 50k around
   96ms/59MB RSS, calls 50k around 116ms/62MB RSS, control 50k around
   120ms/70MB RSS, arrays 50k around 141ms/60MB RSS, and generics 50k around
   143ms/47MB RSS. After that, focused `expressions_50000 --timings` showed
   parse/load still dominated expression-heavy code: roughly 0.63s parse/load
   and 0.36s sema out of about 1.0s total. A small allocation pass reserved
   known child/callee vector sizes for unary, member, index, call, slice,
   conditional, dict-entry, and related expression nodes. Focused
   `expressions_50000 --timings` improved from about 0.996s total with 0.634s
   parse/load to about 0.953s total with 0.584s parse/load. A broad one-sample
   10k/50k Release sweep kept the change: expression-heavy 50k measured about
   980ms/299MB RSS, modules 50k about 110ms/116MB RSS, functions 50k about
   99ms/59MB RSS, calls 50k about 119ms/63MB RSS, control 50k about
   118ms/70MB RSS, arrays 50k about 143ms/59MB RSS, and generics 50k about
   139ms/47MB RSS. Expression-level template argument payloads then moved
   behind sparse pointer storage. Parsed `TypeRef` template arguments were
   already sparse; this removed the mostly-empty expression template-argument
   vector from normal expression nodes. It dropped local `Expr` size from 256
   to 248 bytes and local `Stmt` size from 768 to 752 bytes. Focused timings
   were neutral, so the change is kept as a memory/scale win rather than a
   throughput win. A broad one-sample 10k/50k Release sweep measured
   expression-heavy 50k around 1.02s/292MB RSS, modules 50k around 104ms/113MB
   RSS, functions 50k around 97ms/58MB RSS, calls 50k around 117ms/61MB RSS,
   control 50k around 119ms/69MB RSS, arrays 50k around 141ms/58MB RSS, and
   generics 50k around 140ms/46MB RSS. Expression callees then moved behind
   sparse pointer storage. Calls still keep structured callee AST, but normal
   non-call expressions no longer carry an empty vector. This dropped local
   `Expr` size from 248 to 240 bytes and local `Stmt` size from 752 to 736
   bytes. Focused timings were neutral: expression-heavy 50k stayed around
   0.95s total, with parse/load still the dominant phase. A broad one-sample
   10k/50k Release sweep kept the change as a memory/scale win:
   expression-heavy 50k measured about 1.01s/285MB RSS, modules 50k about
   107ms/112MB RSS, functions 50k about 97ms/57MB RSS, calls 50k about
   118ms/60MB RSS, control 50k about 119ms/68MB RSS, arrays 50k about
   144ms/58MB RSS, and generics 50k about 144ms/47MB RSS. Callgrind on a 5k
   expression-heavy generated file showed parsing at roughly 65% of total
   instruction count and sema around 34%, with libc `memcmp` dominating the
   flat profile. The parser's binary-precedence check then moved from repeated
   `string_view == literal` comparisons to size/character dispatch. Focused
   `expressions_50000 --timings` improved from about 0.95s total and 0.585s
   parse/load to about 0.875s total and 0.505s parse/load. A broad one-sample
   10k/50k Release sweep kept the change: expression-heavy 50k measured about
   932ms/285MB RSS, modules 50k about 100ms/111MB RSS, functions 50k about
   96ms/57MB RSS, calls 50k about 113ms/61MB RSS, control 50k about
   117ms/68MB RSS, arrays 50k about 139ms/58MB RSS, and generics 50k about
   142ms/47MB RSS. A small assignment-target fast path then skipped the full
   expression parser for bare-name assignment targets. This is a common source
   shape and removes unnecessary AST parser work for `name = value`, but the
   measured win is intentionally recorded as minor: focused expression-heavy
   50k moved only from about 0.875s to about 0.871s, while one broad sample saw
   expression-heavy 50k around 918ms/285MB RSS and no meaningful broad
   regression. After removing the direct native build backend and routing build
   commands through generated CMake, a fresh one-sample 10k/50k Release sweep
   again showed expression-heavy code as the outlier: about 902ms/285MB RSS at
   50k lines, while functions/classes/modules/calls/control/arrays/generics
   stayed around 67-139ms at 50k. An empty `SourceFileName` constructor
   short-circuit was tried as hot-path hygiene after callgrind showed many
   empty-source primitive `TypeRef` locations, but a focused three-sample
   Release benchmark showed no meaningful expression-throughput improvement; do
   not count that as a compiler-speed win. A simple primitive numeric
   arithmetic fast path then avoided full alias/operator/assignment
   compatibility checks for exact primitive numeric operands and contextual
   numeric literals, while keeping aliases and complex user/native operators on
   the normal sema path. Focused three-sample Release expression benchmarks
   dropped 10k lines from about 194ms to about 153ms and 50k lines from about
   915ms to about 726ms, with RSS still around 285MB at 50k. A broad one-sample
   shape sweep then measured expression-heavy 50k around 700ms, with
   functions/classes/modules/calls/control/arrays/generics still roughly in
   their previous ranges. A follow-up callgrind run found the expression-heavy
   path was spending most of its load/parse time in accidental `SourceFileName`
   construction: normal lexer comparisons such as `two == "=="` could select a
   reverse `std::string_view == SourceFileName` overload and intern string
   literals millions of times. `SourceFileName` constructors are now explicit
   and only the natural `SourceFileName == string_view` comparison direction is
   exposed. Focused three-sample Release expression benchmarks then dropped 10k
   lines from about 153ms to about 67ms and 50k lines from about 742ms to about
   287ms, with RSS still around 285MB at 50k. The timing split for
   expression-heavy 50k moved load/parse from about +0.53s before sema to about
   +0.17s before sema, with total check around +0.27s. A broad one-sample
   Release sweep measured expression-heavy 50k around 284ms, functions/classes
   around 44-68ms, calls/control/arrays/generics around 67-92ms, and modules
   around 92ms. A token-kind-gated prefix parser then reduced repeated hot
   helper calls in expression parsing; focused three-sample Release expression
   benchmarks moved 10k lines from about 67ms to about 65ms and 50k lines from
   about 287ms to about 273ms. Inlining the expression-token predicate moved
   the focused expression benchmark again to about 61ms at 10k and 258ms at
   50k, with RSS still around 285MB at 50k. A broad one-sample Release sweep
   after both parser changes measured expression-heavy 50k around 261ms,
   functions/classes around 46-72ms, calls/control around 67-81ms,
   arrays/generics around 87-90ms, and modules around 85ms. A postfix parser
   cleanup then removed common `expr_token_begin` rescans from member, index,
   call, and bracket-template-call node construction. That mostly acts as
   parser hygiene for call/member-heavy real code; current generated shapes did
   not show a meaningful isolated throughput change. Replacing `Expr::op`'s
   owned `std::string` with canonical `std::string_view` operator spellings
   reduced `Expr` from 240 to 224 bytes and `Stmt` from 736 to 704 bytes.
   Focused three-sample Release benchmarks moved expression-heavy 50k from
   about 262ms/285MB RSS to about 245ms/272MB RSS, while calls moved about
   67ms/61MB to about 65ms/59MB, arrays stayed around 85ms with lower RSS, and
   generics stayed around 88-91ms with lower RSS. A broad one-sample Release
   sweep after the layout change measured expression-heavy 50k around 245ms,
   functions/classes around 45-70ms, calls/control around 65-80ms,
   arrays/generics around 85-88ms, and modules around 85ms. `join_tokens` then
   gained a same-line fast path so normal single-line statement pieces do not
   rescan every token just to compute source ranges and rediscover that there
   are no layout tokens. A three-sample broad Release sweep moved
   expression-heavy 50k from about 245ms to about 239ms, functions 50k from
   about 70ms to about 68ms, classes 50k from about 45ms to about 42ms,
   control 50k from about 80ms to about 78ms, and modules 50k from about 85ms
   to about 83ms, with memory broadly unchanged from the previous layout win.
   `type_ref_is_name` then stopped routing simple name comparisons through
   `type_ref_head_name`, avoiding a temporary `std::string` in a common sema
   predicate while preserving the old `TypeKind::Named`-only behavior. The
   three-sample broad Release sweep was mixed but mostly useful: calls 50k
   moved about 65ms to 64ms, functions 50k about 68ms to 66ms, modules 50k
   about 83ms to 79ms, arrays stayed around 85ms, expressions stayed around
   239-240ms, and control flow was noisier around 78-82ms. Token/string literal
   comparisons in parser hot paths then gained fixed-size literal overloads so
   checks such as `match_identifier("return")` and `at_operator("*")` do not
   rebuild a `string_view` from a C string and measure the literal every time.
   A three-sample broad Release sweep moved expression-heavy 50k from about
   239-240ms to about 233ms, with calls around 64ms, control around 76ms,
   modules around 80ms, arrays around 85ms, and generics around 91ms.
   Iterator-based full-file reads were then replaced with one shared pre-sized
   file read helper used by the CLI, module loader, formatter, test driver,
   LSP workspace scan, native-header cache, and generated-CMake backend. The
   three-sample broad Release sweep was mostly neutral on frontend throughput:
   expression-heavy 50k stayed around 232ms, while calls/control/arrays moved
   slightly lower and build-driver cases stayed within normal noise. Replacing
   primitive numeric type/operator classification with hand-written
   size/character checks was tried and rejected: a three-sample broad Release
   sweep did not improve expression-heavy code and made calls/control/modules
   slightly worse. Keep
   compiler speed
   validation broad: generated corpora need multiple diverse code shapes,
   because one particular compilation path can dominate or regress while an
   aggregate number looks acceptable. Do not treat a compiler-speed change as
   proven by one synthetic shape; include expression-heavy code, call-heavy
   code, control flow, arrays, modules/imports, generics/templates, class/OOP
   shapes, native interop shapes, and mixed realistic project-shaped corpora as
   the benchmark suite grows. The benchmark harness now includes a `mixed`
   generated shape by default; a local three-sample Release run at
   `--line-scales 50000` measured the mixed shape at about 111ms and 113MB RSS
   for roughly 27.6k generated Dudu lines across nine files. The same run
   measured expression-heavy code at about 231ms, keeping expression
   parsing/sema as the current frontend outlier. Lexer character
   classification then moved from locale-aware C library calls to explicit
   ASCII checks for Dudu source. A local three-sample Release run was
   neutral-to-positive across broad shapes: simple `duc check` moved from
   about 17ms to 14ms, native cached checks from about 18ms to 14ms,
   expression-heavy 50k from about 231ms to 230ms, modules from about 79ms to
   76ms, and mixed from about 111ms to 109ms. Shared parse utilities then got
   the same ASCII treatment for trimming, identifier checks, and numeric
   literal checks. A five-sample focused Release repeat on affected 50k shapes
   was neutral-to-positive after noise settled: functions about 63ms, arrays
   about 82ms, expressions about 228ms, and mixed about 106ms. Parser
   all-caps-identifier checks then got the same explicit ASCII treatment. A
   three-sample broad Release run was neutral rather than a throughput win:
   functions about 63ms, arrays about 83ms, expressions about 233ms, modules
   about 82ms, and mixed about 107ms. Keep this only as source-rule
   consistency, not as a claimed speedup. Local type lookup then gained a
   pointer-returning helper used by the hot `Name` inference branch, avoiding
   unknown-type construction and a `has_type_ref` check on every local hit. A
   five-sample focused Release run was small but non-negative: expressions
   about 233ms, calls about 61ms, control about 73ms, and mixed about 106ms.
   A three-sample broad run measured expressions about 230ms, calls about
   62ms, control about 73ms, generics about 87ms, and mixed about 112ms. Keep
   this as sema hot-path cleanup, not as a major throughput win. Rewriting
   statement-block parsing to reserve first-level statement storage from the
   token stream was then kept as a small parser allocation cleanup. It targets
   large single-function and large block-shaped files, not native-heavy code.
   A five-sample focused Release run measured expression-heavy 50k at about
   230ms, calls at about 60ms, control at about 74ms, and mixed at about
   103ms. A three-sample broad Release repeat measured expressions about
   225ms, calls about 58ms, modules about 76ms, arrays about 83ms, generics
   about 91ms, and mixed about 105ms. Treat this as a modest broad-neutral
   win; it does not remove the deeper expression-AST allocation bottleneck.
   Statement classification then stopped scanning a line after the first
   top-level assignment operator, because no later colon can make that line a
   typed declaration. This avoids walking long right-hand expressions just to
   classify ordinary assignments. A focused five-sample Release run measured
   calls at about 57ms, control at about 74ms, expressions at about 225ms, and
   mixed at about 104ms. The broad three-sample repeat kept calls around 58ms,
   control around 73ms, arrays around 83ms, modules around 75ms, and mixed
   around 107ms; expression-heavy landed at about 231ms, so treat this as a
   statement-scan cleanup for assignment-heavy code, not as the expression-AST
   fix.
   A massif heap profile of `duc check` on the 50k expression shape showed the
   remaining memory problem clearly: peak useful heap was about 272MB. The
   dominant retained buckets were lexer token storage at about 74MB, binary
   expression parsing/child storage at tens of MB and still the largest parse
   bucket at peak, statement-block storage at about 35MB, and optional
   assignment-target expression storage at about 12MB. Do not spend more time
   on tiny string scans before addressing the compact-AST/token representation
   problem. `Expr.name`/`Expr.value` cannot be naively changed to
   `std::string_view` without an ownership plan, because native escape and
   synthesized AST paths can create temporary strings. The likely serious fixes
   are an interned source text/symbol store, compact token/source-position
   storage, and arena or compact child storage for expression trees.
   `SourceFileName` then moved from an interned string pointer to a compact
   32-bit intern id while preserving the existing API. This shrinks every
   `SourceLocation`, `SourceRange`, token, and AST node that stores locations.
   A five-sample focused Release run measured expression-heavy 50k at about
   227ms and 265MB RSS, calls at about 60ms and 52MB RSS, control at about
   73ms and 64MB RSS, and mixed at about 101ms and 103MB RSS. A three-sample
   broad Release repeat measured expressions about 221ms and 265MB RSS,
   calls about 57ms and 52MB RSS, functions about 59ms and 48MB RSS, modules
   about 76ms and 105MB RSS, generics about 88ms and 44MB RSS, and mixed
   about 105ms and 103MB RSS. Keep this as a real representation cleanup: it
   reduces memory broadly and improves the expression-heavy outlier without
   changing language behavior.
   Expression optional side fields then moved from `shared_ptr` to
   `unique_ptr` with explicit deep-copy semantics. This shrinks `Expr` from
   216 bytes to 184 bytes and `Stmt` from 680 bytes to 616 bytes in the
   current layout. Unlike the rejected statement-level ownership experiment,
   focused and broad Release sweeps stayed positive: a five-sample focused
   run measured expression-heavy 50k at about 213ms and 238MB RSS, calls at
   about 56ms and 49MB RSS, control at about 71ms and 59MB RSS, and mixed at
   about 100ms and 99MB RSS. A three-sample broad sweep measured expressions
   about 208ms and 238MB RSS, calls about 58ms and 49MB RSS, functions about
   56ms and 45MB RSS, classes about 41ms and 33MB RSS, modules about 75ms and
   99MB RSS, arrays about 79ms and 51MB RSS, generics about 87ms and 41MB
   RSS, and mixed about 105ms and 99MB RSS. Keep validating this kind of
   representation change against diverse generated shapes; the win is broad
   memory reduction plus a real expression-heavy improvement, not a one-shape
   trick.
   Expression operators then moved from a raw `string_view` to a compact
   one-byte operator code with text conversion at API/native boundaries. This
   shrinks `Expr` again from 184 bytes to 176 bytes and `Stmt` from 616 bytes
   to 600 bytes. A five-sample focused Release run measured expression-heavy
   50k at about 212ms and 232MB RSS, calls at about 57ms and 49MB RSS,
   control at about 72ms and 59MB RSS, and mixed at about 104ms and 97MB RSS.
   A three-sample broad Release run measured expressions about 211ms and
   232MB RSS, calls about 57ms and 49MB RSS, modules about 75ms and 98MB RSS,
   arrays about 79ms and 50MB RSS, generics about 88ms and 41MB RSS, and
   mixed about 107ms and 97MB RSS. Treat this as a memory/layout cleanup with
   neutral-to-noisy timing, not a speed breakthrough; keep it only while full
   validation and future diverse sweeps stay clean.
   `SourceRange` then stopped storing a second full `SourceLocation` for its
   end point. The end point is now a fileless line/column `SourcePosition`,
   and code that genuinely needs a full end location must reconstruct it from
   the range start. This shrinks `SourceRange` from 24 bytes to 20 bytes,
   `TypeRef` from 136 bytes to 128 bytes, `Expr` from 176 bytes to 168 bytes,
   and `Stmt` from 600 bytes to 576 bytes; `Token` remains 40 bytes due to
   alignment. A five-sample focused Release run measured expression-heavy 50k
   at about 212ms and 225MB RSS, calls at about 58ms and 47MB RSS, control at
   about 72ms and 57MB RSS, and mixed at about 100ms and 93MB RSS. A
   three-sample broad Release run measured expressions about 209ms and 225MB
   RSS, calls about 57ms and 47MB RSS, functions about 57ms and 44MB RSS,
   classes about 42ms and 32MB RSS, modules about 74ms and 95MB RSS, arrays
   about 79ms and 49MB RSS, generics about 85ms and 40MB RSS, and mixed about
   101ms and 94MB RSS. Keep it as another representation cleanup: it reduces
   memory broadly, but the throughput effect is still mostly noise.
   `Expr.name`, `Expr.value`, `TypeRef.name`, and `TypeRef.value` then moved
   from owning `std::string` fields to compact interned `SourceTextAtom`
   values. The constructors must stay explicit: implicit construction polluted
   normal `std::string_view == "literal"` comparisons through ADL and caused
   widespread overload ambiguity. This shrinks `TypeRef` from 128 bytes to 72
   bytes, `Expr` from 168 bytes to 104 bytes, and `Stmt` from 576 bytes to 448
   bytes. A five-sample focused Release run measured expression-heavy 50k at
   about 205ms and 177MB RSS, calls at about 52ms and 41MB RSS, control at
   about 63ms and 47MB RSS, and mixed at about 89ms and 70MB RSS. A
   three-sample broad Release run measured expressions about 204ms and 177MB
   RSS, calls about 52ms and 40MB RSS, functions about 52ms and 38MB RSS,
   classes about 41ms and 28MB RSS, modules about 65ms and 75MB RSS, arrays
   about 71ms and 40MB RSS, generics about 79ms and 33MB RSS, and mixed about
   87ms and 70MB RSS. This is a real broad win, not just a one-shape memory
   cleanup; keep watching native-heavy and LSP paths because interner lookup
   behavior is now on more hot paths.
   `SourceTextAtom` equality between two atoms then switched from resolving
   both interned strings to comparing intern ids directly. This keeps the
   external string-like API but avoids mutex-backed table reads for atom-to-atom
   comparisons. A five-sample Release run on indexing/operators/matches plus
   arrays/generics/mixed measured operators around 94ms, matches around 61ms,
   generics around 80ms, and mixed around 88ms; indexing remained noisy around
   134ms, so this is a small representation cleanup, not the indexing fix.
   A thread-local last-id cache inside `source_text_from_id` was then kept.
   It preserves the mutex-backed interner lookup for misses while avoiding the
   lock/table path when hot code resolves the same atom repeatedly. A
   five-sample focused Release run measured indexing 50k at about 128ms,
   arrays about 68ms, generics about 78ms, operators about 94ms, and mixed
   about 86ms. A three-sample broad run stayed acceptable, with expressions
   about 202ms, functions about 52ms, modules about 66ms, indexing about
   132ms, operators about 93ms, and mixed about 93ms. This is still not a
   complete indexing fix, but it reduces the hot interner read cost without
   changing AST size or source semantics. A broader static-atom rewrite of
   `lower_template_type` was tried separately, and then retried after this
   lookup cache landed, and was not kept: it still regressed indexing,
   generics, and mixed project-shaped code compared with the simpler
   cache-only path.
   A narrower `lower_template_type` cleanup then kept local template names as
   `std::string_view` while leaving fallback emission paths string-based. This
   avoids repeated `std::string == "literal"` dispatch in a hot template
   lowering path without reintroducing the rejected static-atom rewrite. A
   five-sample focused Release run measured indexing 50k around 128ms, arrays
   around 68ms, generics around 77ms, matches around 62ms, operators around
   93ms, and mixed around 88ms. A three-sample broad Release run stayed in the
   same band: expressions around 200ms, calls/functions around 52ms,
   classes around 40ms, modules around 66ms, arrays around 69ms, generics
   around 79ms, indexing around 131ms, matches around 64ms, operators around
   92ms, and mixed around 91ms. Keep this as a small hot-path cleanup, not as
   a major compiler-speed milestone.
   The source-text atom lookup cache then expanded from a one-entry
   thread-local cache to a tiny direct-mapped cache. This keeps the same
   mutex-protected interner miss path but helps compiler code that alternates
   between several AST names instead of repeatedly touching one atom. A
   five-sample focused Release run measured arrays 50k around 65ms, indexing
   around 124ms, generics around 73ms, matches around 57ms, operators around
   87ms, and mixed around 85ms. A three-sample broad Release run stayed clean:
   expressions around 198ms, calls around 51ms, classes around 38ms, control
   around 60ms, modules around 58ms, arrays around 67ms, indexing around
   125ms, generics around 73ms, matches around 60ms, operators around 90ms,
   and mixed around 86ms. Keep it as a real broad frontend-throughput win.
   Increasing that direct-mapped source-text atom cache from 16 to 64 entries
   was then kept after another diverse Release sweep. A focused five-sample
   run measured expressions 50k around 200ms, indexing around 125ms, matches
   around 57ms, operators around 87ms, and mixed around 89ms. A three-sample
   broad run stayed positive: expressions around 194ms, calls around 50ms,
   classes around 38ms, control around 64ms, modules around 62ms, arrays
   around 66ms, indexing around 124ms, matches around 61ms, operators around
   90ms, and mixed around 89ms. Keep treating cache-size tuning as measured,
   not guessed; larger cache sizes need their own diverse validation.
   Replacing the arithmetic/comparison string helper calls in
   `sema_expr_type_ops` with local `ExprOpCode` switch helpers was tried and
   rejected. Although the AST already stores compact operator codes, the local
   rewrite regressed the focused five-sample Release shape set: expressions
   moved to about 202ms, indexing to about 130ms, generics to about 83ms, and
   operators to about 89ms. Do not repeat that exact helper rewrite without a
   deeper profile showing operator text conversion is again dominant.
   Changing `named_type_ref` from an owning `std::string` parameter to
   `std::string_view` was tried and rejected even though it looked like an
   obvious way to avoid temporary strings before atom interning. Focused
   five-sample Release results regressed the same shape set: arrays around
   69ms, indexing around 129ms, generics around 79ms, matches around 62ms,
   operators around 92ms, and mixed around 86ms. The current by-value API may
   still be inelegant, but this local signature change is not a throughput win.
   Collapsing the index-expression local receiver path from `contains` plus
   lookup to a single pointer lookup was also tried and rejected. It looked
   like a clear cleanup, but focused five-sample Release results regressed
   indexing to about 128ms and mixed project-shaped code to about 89ms while
   providing no broad win. The repeated map lookup is ugly, but this local
   rewrite changes enough control flow that it is not a safe performance
   improvement.
   Replacing statement-level optional `shared_ptr` fields with `unique_ptr`
   plus deep-copy semantics was tried and rejected. It reduced some retained
   memory in expression/call/control shapes, but a five-sample focused Release
   run made expression-heavy 50k slower at about 235ms and increased the mixed
   project-shaped RSS to about 118MB. The deep-copy behavior is the wrong
   ownership shape for the current AST; revisit only as part of a broader
   compact/arena AST design where statement copies are controlled explicitly.
   A narrower variant that changed only `Stmt::type_ref` from `shared_ptr` to
   `unique_ptr` was also tried and rejected. It shrank `Stmt` from 448 bytes
   to 440 bytes after text interning, but same-session A/B benchmarks across
   expressions, calls, control, mixed, modules, generics, classes, and arrays
   did not show a reliable speed win. The confirming pass regressed calls,
   classes, and mixed project-shaped code while only giving small/noisy RSS
   movement. Do not keep tiny AST-size wins that add ownership/copy
   complexity unless diverse generated shapes show a repeatable throughput or
   memory improvement.
   Rewriting
   `sema_context::trim` from front-erasing to substring bounds plus ASCII
   checks was tried and rejected: focused repeats regressed modules and mixed
   project-shaped code and did not preserve an expression-heavy win. Lazily
   computing `infer_expr_type_ast` diagnostic locations was also tried and
   rejected: the expression-heavy target did not improve, and broad generated
   shape differences were noise-level. A last-local-type lookup cache inside
   `FunctionScope` was tried and rejected: it made expression-heavy code,
   arrays, functions, control flow, and mixed project-shaped code worse despite
   slightly helping generics. A fixed-array rank helper in `sema_index` that
   avoided repeated numeric/symbolic shape extraction for slice cases was tried
   and rejected after the new indexing benchmark shape landed: the targeted 50k
   indexing shape stayed around 132ms in a five-sample Release run and did not
   show a clear win over baseline noise. The repeated shape extraction looks
   ugly, but the current bottleneck is not fixed by that small helper. The
   changed-file case runs against a copied fixture under `build/bench_compiler`
   so benchmarks do not mutate checked-in examples. It
   records source line/file counts and peak child-process RSS in KB with each
   sample. This is a baseline harness, not a pass/fail gate; thresholds, larger
   synthetic corpora, and deeper clean/no-op/one-file-changed generated-CMake
   breakdowns remain later benchmark expansions. `dudu-webserver` dogfood found
   a native-header cache regression where dependency stamps recorded the
   generated scanner `.cpp`, then deleted it, making every later process treat
   the cache as stale. The scanner now records full system-header dependencies
   with `-MD` and explicitly ignores only the generated scanner source. On the
   local webserver project, a touched `.dd` rebuild dropped from about 25.6s to
   about 9.9s on first cache population and about 3.9s on the next process run
   with `native-scan-cache` hits. Generated CMake module emission now records
   the parsed `dudu.toml` as an emit dependency as well as the loaded `.dd`
   sources, so manifest-only Dudu settings can invalidate generated artifacts.

12. Incremental Build Strategy

   Move beyond generated-one-file builds where needed. Generate C++ per Dudu
   module so CMake/Ninja can rebuild changed translation units instead of whole
   programs.

   Status: low-level `duc build <file.dd>` preserves generated C++ mtimes and
   skips the C++ compile/link step when the generated C++, native build command,
   and native source inputs are unchanged. `duc emit`/`duc build` still use one
   generated C++ translation unit for compiler-driver compatibility;
   source-tree module units are preserved in the AST, and per-module
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
   generated translation unit. Generated artifact writes preserve mtimes when
   content is unchanged, and generated-CMake uses a stamp output plus generated
   file byproducts, so whitespace-only `.dd` edits do not force unchanged
   generated C++ translation units to recompile. Project builds use this CMake
   path; `[build] backend = "direct"` is no longer a supported manifest mode.
   The old implicit single-file shortcut to the direct backend has also been
   removed, so `dudu build` and `dudu run` exercise generated/user-owned CMake
   for small projects too.

   Remaining incremental work is on the Dudu side: `duc emit-modules` still
   analyzes the selected entry's full module graph in a fresh process. Native
   header raw and scan caches are dependency-stamped before disk or process
   cache hits are trusted, and scanner-generated source files must not
   participate in those stamps. Real compiler-speed work still needs
   module-level invalidation and broader structured native-header metadata
   reuse.
   `duc emit-modules --timings` and `duc emit-test-modules --timings` now print
   detailed analyze/load/config/native-merge/sema/emit progress, which also
   makes generated-CMake custom command stalls visible when `dudu build
   --timings` enables `DUDU_TIMINGS`.

13. Language Server And Formatter

   Implement `duc lsp` so editors can show diagnostics, warnings, hover,
   completion, go to definition, find references, rename, formatting, and native
   header navigation. The concrete plan is
   [Language Server Plan](language-server-plan.md).

   Build LSP and formatter behavior from the same AST/sema model used by the
   compiler.

   Status: unaliased nested Dudu module imports such as
   `import vendor.helper` now resolve through their full dotted path for hover,
   go-to-definition, and member completion, matching the compiler's
   Python-shaped module binding behavior. Rename is declaration-anchored across
   open and unopened workspace files, but skips files that declare their own
   same-named Dudu symbol until cross-file symbol identity is strong enough to
   prove those edits are related. Current-document unqualified call-site rename
   is allowed when the call resolves to one renameable Dudu declaration in that
   document and no visible local type binding shadows it; ambiguous use-sites
   remain rejected until symbol identity is strong enough to avoid editing
   unrelated same-named locals. Declaration-anchored references use the same
   conservative redeclaration filter, while imported module/member references
   keep workspace search. LSP diagnostics now load multi-file module trees with
   the open editor buffer as the entry source, so unsaved fixes to imports or
   entry code are analyzed consistently with the saved dependency modules
   instead of rereading a stale entry file from disk. Member completion,
   member-definition lookup, native alias hover, top-level completion, and
   signature help now use the same shared LSP module view, including imported
   Dudu module shapes, visible imported functions, and native headers. Fields
   and methods on imported classes such as `from vec3 import Vec3` are visible
   to the editor, selectively imported functions participate in completion and
   call signature help, and `module.` completion, imported hover, plus import
   go-to-definition resolve through loaded module units instead of manually
   reparsing imported files. Module-qualified reference searches such as
   `m.answer` now resolve the module alias target before searching the
   workspace, so unrelated files that reuse the same alias for a different
   module are not reported as references. Aliased native function references
   such as `dudu_native.dudu_native_add` keep the full native path during
   reference lookup. LSP symbols now carry native identity keys for scanned
   native declarations, including C++ class methods, preserving canonical
   native identity at the editor boundary so native references can move away
   from plain name matching. Find-references uses those native identity keys to
   filter same-spelled native references across workspace documents when
   identity metadata is available, so two headers imported under the same alias
   with the same function name are not conflated. Definition and hover symbol
   lookup now prefer exact symbols and only use suffix matches when
   unambiguous; receiver-aware member definition and hover run before suffix
   fallback, so same-named native methods resolve through the expression
   receiver type instead of scan order.
   Cursor selection for definition and reference queries now runs through a
   shared AST-backed selection pass that records the simple symbol, dotted
   symbol path, and expression path together. This removes duplicated parse/walk
   work from the hot editor path and keeps future hover/reference/rename
   improvements on one cursor-selection model.
   The LSP hover request path now also passes that selected expression path into
   hover handling instead of reparsing the document to recover member-hover
   context.
   Reference and rename scope decisions now also consume the shared selection
   result instead of reselecting the same symbol/path.
   Rename call-site detection records call-callee selection during the same AST
   walk, removing another private parse from rename scope checks.
   Find-references keeps unresolved member expressions as dotted queries instead
   of falling back to the bare member name, so unrelated same-named member calls
   are not reported together. Module-qualified references now also include the
   declaration in the target module by searching that module with the unqualified
   imported member name. Selective `from module import name as alias`
   references now use the same resolved module target, include the original
   declaration, and exclude other files that reuse the alias for a different
   module.

   Documentation hover needs a real documentation model, not only line scanning.
   Dudu should support Python-shaped contiguous `#` declaration comments and
   Python-style triple-single-quoted `''' ... '''` docstrings as the first
   statement inside the declaration body for larger module, class, method,
   function, enum, field, constant, and alias docs. The language server should
   attach these docs to AST declarations, preserve them across module imports,
   and show them in hover, completion resolve, signature help, and document
   symbols. Native C/C++ hover should also surface header comments when
   Clang/header metadata can recover them, while falling back honestly to
   signature-only hover when documentation is unavailable. The concrete plan and
   remaining implementation checklist live in
   [Language Server Plan](language-server-plan.md#doc-comments-and-docstrings).

14. Project Driver Polish

   Keep using `dudu` on real projects and fix friction in native build inputs,
   target selection, diagnostics, generated build files, and examples.

   `dudu build`, `dudu run`, and `dudu test` are the serious user-facing
   commands. Backend choice is an implementation detail for normal users. The
   generated-CMake backend is the standard Dudu project backend because it emits
   separate generated files and cooperates with the C/C++ ecosystem. The direct
   backend is an explicit low-level compiler/debug backend, not the normal
   project path. `dudu cmake` remains an inspectable artifact/debug/handoff
   command, not the primary serious-project workflow.
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
   command. Generated CMake is the standard project backend. Direct compilation
   is the explicit small/debug backend. `dudu cmake` is the inspectable artifact
   and native handoff path, not the replacement for the normal command surface.

   This is not a Zig-style attempt to make the language build system own every
   native project. Zig's normal model is a `build.zig` graph that owns the
   build and can compile/link C and C++ inputs through that graph. Zig build
   scripts can run external tools when users write that integration, but Zig
   does not have a built-in "revert to CMake" backend. Dudu's native interop
   goal is broader in a different direction: generated CMake builds and
   user-owned CMake builds are project backend modes behind the same command
   surface. The old direct native build backend has been removed; `duc` stays
   valuable for low-level check/format/emit/debugging commands, not as a
   separate native build path.
   CMake-backed builds should still be launched through `dudu build`,
   `dudu run`, and `dudu test`.

   Dudu is not copying Zig here. CMake is not a shameful fallback after
   `dudu build` gives up. It is one of the serious native backends because
   CMake is where a large part of the C/C++ ecosystem already lives.

   `dudu.toml` remains the canonical Dudu project file, but it does not have
   to be the canonical source for every native compilation detail in a mixed
   C/C++ project. The manifest should own Dudu entries, targets, generated
   artifact locations, Dudu compile-time settings, delegated commands, and
   backend selection. User-owned CMake may remain the authority for native
   target definitions, toolchain files, platform conditionals, package
   discovery, install/export rules, vendored native dependencies, CUDA or
   platform-SDK setup, and other details that CMake already models well.

   This boundary is deliberate. Trying to force all native build state into
   `dudu.toml` would make Dudu slowly recreate CMake and would make serious
   interop worse. The serious contract is that `dudu build` orchestrates the
   selected backend, emits Dudu artifacts, streams useful progress, and fails
   clearly when the selected backend cannot represent the project. The native
   backend is allowed to own native build authority; the user should still be
   able to enter through Dudu's commands.

   File ownership must stay clean. Generated-CMake mode may overwrite Dudu's
   internal generated CMake project under the build directory. User-owned CMake
   mode must never patch or rewrite a user's `CMakeLists.txt`, including magic
   commented Dudu blocks. Dudu should instead emit stable generated `.hpp/.cpp`
   artifacts and, when useful, a small generated CMake include/source-list file
   that user CMake can consume explicitly. `dudu init` may create an initial
   CMake file, but after creation that file belongs to the user.

   Near-term distribution should stay in the dev/source-install lane until the
   CLI and compiler pipeline are boring. Add a real local install path first:
   CMake install targets, `cmake --install`/`make install` support where the
   generator provides it, and a `scripts/install-local.sh` that works on Linux
   and macOS, including Apple Silicon Macs. That script should install or link
   `dudu`, `duc`, the language server, runtime/prelude headers, editor support,
   and any required CMake/toolchain integration files from a checkout without
   pretending to be a package manager. Tagged GitHub releases can then ship
   source tarballs and maybe Linux/macOS binaries. Homebrew, AUR, apt, winget,
   and similar package-manager distribution should wait until command behavior,
   versioning, changelog practice, and dependency requirements are stable.

   Status: CMake install targets now install `dudu`, `duc`, docs, and editor
   support. `scripts/install-local.sh` configures, builds, and installs this
   checkout to `~/.local` by default or a caller-provided `--prefix`; it uses
   the same CMake install rules instead of acting as a package manager.
   `dudu init` and `dudu new` have fast acceptance coverage proving they create
   runnable hello projects and follow the documented git/`.gitignore` behavior
   inside and outside an enclosing repository. They also create a starter
   user-owned `CMakeLists.txt` that builds generated Dudu module artifacts
   through `duc`; the fast smoke configures and builds that file with CMake.
   `dudu build`, `dudu run`, and `dudu test` use the generated-CMake backend by
   default, even for single-module inputs, so generated C++ stays split into
   per-module artifacts. The project configuration model also defaults to
   CMake; `duc build` and `duc run` also use generated CMake rather than a
   separate single-file native compiler shortcut. `[build] backend = "cmake"`
   is the only supported manifest backend; `[build] backend = "direct"` is
   rejected. The generated CMake
   backend is implemented for `dudu build`, `dudu run`, and `dudu test`; it
   emits an internal CMake project and drives `cmake -S/-B`
   plus `cmake --build`. Native inputs such as include paths, library paths,
   libraries, flags, pkg-config packages, and extra C/C++ sources are covered
   by the generated-CMake backend for normal project shapes. The
   generated-CMake fixture links an extra C source into both the app target and
   generated Dudu test harness. `dudu cmake` still emits CMake for inspection or
   handoff. User-owned CMake build/run/test are implemented for manifests that
   declare `[cmake] source` and `[cmake] target`; the driver configures and
   builds the existing CMake project under the Dudu build directory, and runs
   CTest when no Dudu test entry or explicit delegated test command is present.
   The project-driver front door now streams native compiler, CMake, and CTest
   output for `dudu build`, `dudu run`, and `dudu test` while keeping full
   command lines behind `--verbose`. `dudu build` reports the final artifact
   path with an `output` line for generated/user-owned CMake backends.
   Generated-CMake executable/library builds support `-o` by copying the built
   CMake target artifact to the requested path after a successful build, while
   user-owned CMake keeps artifact naming under the user's CMake project.
   Generated-CMake builds report `backend`, `entry`, `cmake`, `build`, and
   final `output`/`run` stages before delegating to CMake so normal builds show
   where time is going.
   Project-driver commands accept `--timings`, which prefixes Dudu progress
   lines with elapsed seconds from command start. This makes slow phases such
   as full-module analysis or native header metadata parsing visible without
   timestamping user program stdout.
   `dudu run ... -- args...` now forwards trailing arguments to the launched
   executable and prints them in the `run` step, while preserving compiled
   program stdout for the program itself.
   `--quiet` suppresses project-driver
   progress output for scripts. `dudu check` now prints `check` and `ok` lines
   on successful validation unless `--quiet` is set, while `duc check` remains
   quiet for direct compiler-driver scripts. `dudu fmt` formats either the
   current project tree or an explicit file in place by default, while `duc fmt`
   keeps the direct stdout-oriented formatter behavior. `dudu bench` is
   benchmark from a checkout, and benchmark command arguments remain available
   after `--`.
   The generated CMake test backend now uses `duc emit-test-modules` to emit
   per-module test-mode `.hpp/.cpp` artifacts plus a small generated
   `test_harness.cpp`; generated module sources suppress normal executable
   entry points and headers expose test functions to the harness. Regression
   fixtures cover generated CMake run/test for a multi-module project whose
   dependency module imports and calls a native C++ header through normal Dudu
   header awareness.
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
   Delegated `[test]` and `[bench]` commands, plus fallback project scripts,
   now run from the manifest directory so project-relative shell commands do not
   accidentally depend on the caller's current directory. Quoted manifest
   strings decode common TOML escapes such as `\"`, `\\`, and `\n`, including
   inside string arrays, so command strings and native paths do not leak raw
   escape backslashes into the driver. Invalid or unfinished quoted-string
   escapes are rejected as manifest errors instead of being guessed. The stale
   `[cmake] enabled` key has been removed; CMake behavior is selected through
   `[build] backend = "cmake"` plus `[cmake] source`/`target` for user-owned
   CMake projects, or through `dudu cmake` for inspectable CMake emission.

15. Freestanding And Embedded Assert Policy

   Hosted `assert` and `debug_assert` are implemented. Freestanding and
   embedded targets reject runtime `assert` instead of accidentally emitting
   hosted runtime machinery.

   Status: implemented. Semantic checking rejects runtime `assert` in
   `freestanding` and `embedded` target modes with a Dudu-source diagnostic
   that recommends `debug_assert` or a target-specific assert handler.
   `debug_assert` remains available and lowers to native C/C++ assertion
   behavior.

16. Macro Edge Cases

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

17. Remove Prototype Cruft

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
   intentionally narrow low-level `duc` merged translation unit. No active
   dunder/operator compatibility path or stale inline-function-literal path was
   found in the implementation. Optional expression holes now use a dedicated
   `Missing` expression kind; `Unknown` is reserved for unsupported source text
   that should be diagnosed rather than interpreted.

18. Compiler Readability And Style Pass

   Do a deliberate readability pass after the major compiler behavior is green.
   Rapid language work leaves behind small local oddities that are not exactly
   compatibility cruft but still make the compiler harder to trust and maintain.
   This is the pass for silly successful-experiment residue: one-line wrappers,
   always-false generic helpers, phase-crossing shortcuts, and spaghetti seams
   that were useful during a milestone but should not survive in the real
   compiler.

   Treat this as a real cleanup phase, not a casual refactor bucket. After a
   project has been worked hard for a while, even good feature work leaves
   behind code that only made sense for the day it was written: tiny forwarding
   helpers, duplicated near-identical checks, old names, "just make it pass"
   branches, and local abstractions that do not describe a real compiler
   concept. Those should be found deliberately and removed deliberately.

   This pass is mandatory because it is a normal part of building a real
   compiler, not a judgment on any one change. When a project moves quickly,
   helper functions and local workarounds accumulate around whatever feature was
   being stabilized that day. After the feature is proven, revisit the shape of
   the code and remove the scaffolding that no longer carries its own weight.

   Also clean these up opportunistically when they are discovered during other
   work. Do not leave obviously silly artifacts in place just because the
   current feature passes. If a helper, branch, or wrapper only exists because
   an earlier implementation was awkward, either delete it immediately or add a
   concrete cleanup note here with the file/function name.

   Apply the same bar to examples, fixtures, and dogfood repos used to validate
   Dudu. If a sample has pass-through functions, fake abstractions, redundant
   getters, unnecessary namespace noise, or old workaround shapes, clean it up
   instead of treating the awkward code as a language limitation.

   Audit for:

   - repo-wide silly artifacts found by targeted searches for one-line helper
     bodies, constant-return helpers, compatibility wording, and stale phase
     names
   - one-line wrapper functions that only obscure the real operation
   - one-line functions whose body is just another function call with the same
     arguments, unless they define a public API, phase boundary, or native
     boundary
   - generic helpers that exist only to return a constant or route one special
     case through template machinery
   - generic helpers that do not use their template parameter for real generic
     behavior
   - duplicate helper logic that should live in one shared file
   - stale names that describe an old implementation instead of current
     behavior
   - over-broad utility functions used in only one narrow place
   - accidental abstractions introduced while keeping intermediate commits green
   - one-line pass-through APIs that were created to keep a change small but do
     not represent a real compiler phase, ownership boundary, or public surface
   - helper names that say what the old implementation did instead of what the
     current compiler concept is
   - feature-specific plumbing that should become a normal AST, sema, module, or
     codegen path before more features depend on it
   - long files that should be split along real compiler phases or ownership
     boundaries
   - comments that narrate obvious code instead of explaining non-obvious
     compiler behavior
   - confusing "temporary", "fallback", or "legacy" language that should either
     become an explicit boundary or be deleted
   - single-call wrappers around generated-code, native-scanner, or sema paths
     that only hide ownership or phase boundaries
   - "template-looking" code that is not actually generic and should be a normal
     typed helper or a direct branch
   - impossible branches and placeholder defaults that quietly return a generic
     value instead of making the compiler invariant visible
   - old test fixtures that pass by leaning on obsolete syntax or accidental
     permissiveness instead of the current language rules
   - examples with hand-written workaround code that should be deleted once the
     language feature or header-awareness path supports the direct form

   This pass is not a license for broad cosmetic churn. Each cleanup should make
   code easier to reason about, reduce duplication, remove misleading
   abstraction, or make a compiler boundary more explicit. Keep behavior
   unchanged unless the cleanup exposes a real bug, then add a regression test.
   Prefer many small cleanup commits tied to concrete ownership improvements
   over one giant style rewrite.

   Examples from recent work: a generic `compatible_native_redeclaration(...)`
   fallback that always returned `false` was replaced with an explicit
   `if constexpr` native-type check and a shared
   `native_type_redeclarations_compatible(...)` helper. That kind of silly
   rapid-iteration artifact should be cleaned up whenever it is spotted.
   Module path/name generation has also been split out of `module_loader.cpp`
   into `module_names.*`, and the LSP now shares that helper instead of carrying
   a duplicate module-path resolver. Index and slice expression emission has
   been split out of `cpp_expr_emit.cpp` into `cpp_expr_index.*`, bringing the
   main expression dispatcher back under the project file-size rule while
   preserving one owner for indexing behavior. Native header dedupe and merge
   now spell the opaque native type redeclaration rule directly at the collision
   branch instead of carrying duplicated tiny generic compatibility lambdas.
   Generic-parameter symbol extension now lives in `sema_common.*` instead of
   separate declaration/body-sema copies, so generic scope setup has one owner.
   C++ template type lowering has been split out of `cpp_type_ref.cpp` into
   `cpp_type_templates.*`, keeping template-name mapping separate from the
   core `TypeRef` dispatcher.
   The stale `native_import_prefixes`/`native_import_path_prefix` names were
   renamed to `native_path_prefixes`/`is_native_path_prefix` after broad
   `alias.anything` native-call acceptance was removed; the name now describes
   known native member paths rather than import aliases. CMake project
   emission no longer reparses `dudu.toml` from the input path just to recover
   the project directory; it uses the `ProjectConfig` already selected by the
   driver, keeping manifest ownership in the project-driver phase. Native
   header scan cache type/record serialization has been split into
   `native_header_cache_format.*`, making the cache format boundary explicit
   and bringing `native_header_cache.cpp` back under the project file-size
   guideline. Match-statement sema no longer uses a generic `std::function`
   callback adapter to recurse into block checking; the API now takes an
   explicit typed block-check context, and the migration guard rejects the old
   callback wrapper name. LSP references and navigation code now share AST
   type/expression/statement traversal helpers instead of carrying duplicated
   recursive walker lambdas in each feature, and cursor selection now lives in
   `language_server_selection.*` instead of navigation utilities so definition
   and reference lookup share one AST selection result. Local function-type alias
   resolution now uses an explicit helper instead of a recursive
   `std::function` lambda, keeping sema recursion named after the compiler
   concept it implements. Core AST expression/statement visitors are now
   template traversal helpers instead of compiled `std::function` callback
   wrappers, leaving `std::function` usage to generated C++ function types and
   the emitted prelude. Native header scanner command/source construction now
   lives in `native_header_scan_command.*`, keeping clang invocation, temp-file
   IO, pkg-config flag collection, and scanner error text out of the native
   declaration merge/orchestration file. Inheritance signature, abstract-method,
   and multiple-inheritance traversal helpers now live in
   `sema_inheritance_internal.*`, leaving `sema_inheritance.cpp` focused on the
   public inheritance sema API. Parsed `cpp(...)` escape inference helpers now
   live in `sema_expr_cpp_escape_infer.*`, leaving
   `sema_expr_cpp_escape.cpp` focused on the public escape-expression
   dispatcher.

19. Polish Unsupported Syntax Diagnostics

   Deliberately unsupported Python-looking syntax should fail hard with
   specific guidance. These are not compatibility paths and they should not
   lower, but diagnostics should explain the Dudu-shaped replacement instead of
   only saying "unsupported".

   Required diagnostic coverage:

   - `lambda`: declare a named function and pass the function name
   - RHS/local `def`: move the function to top-level or class scope, then pass
     the function name as a value
   - ternary conditional expressions: use an explicit `if` statement
   - comprehensions: use an explicit loop and append/insert into a declared
     container
   - `yield` and generators: use an explicit iterator/state type or callback
   - `with`: rely on RAII object lifetime or write the lifetime explicitly
   - `eval` and `exec`: dynamic execution is not part of Dudu
   - `getattr` and `setattr`: use statically known fields or methods
   - rejected OOP decorators such as `@staticmethod`, `@classmethod`, and
     `@property`: use Dudu's class rules instead

   The parser may keep narrow recognition for these forms only to produce these
   diagnostics. It must not preserve alternate syntax, hidden lowering, or
   partial semantic support for them.

   Status: implemented for the documented unsupported Python surface. Negative
   fixtures cover `lambda`, RHS/local `def`, ternary conditional expressions,
   list/dict/set/generator comprehensions, `yield`, `with`, `eval`, `exec`,
   `getattr`, `setattr`, and rejected OOP decorators, with diagnostics that name
   the Dudu-shaped replacement where one exists.

20. Delete AST Migration Fallbacks

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

   Status: complete for the migration-safety definition above. The codebase
   audit and `scripts/check_ast_migration_guards.sh` show no compiler-internal
   callback or public semantic API that accepts rendered Dudu source/type text
   when structured `Stmt`, `Expr`, `TypeRef`, token, or symbol-table facts are
   available. Remaining string use is confined to identifiers, literal/operator
   spelling, diagnostics/display, formatter trivia, explicit `cpp(...)` escape
   payloads, emitted C++, and explicitly named native C/C++ boundary metadata.

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
   Native overload matching no longer exposes callback adapter typedefs for
   expression inference or assignment checks. It calls the structured semantic
   functions directly, and codegen-side native call inference constructs a
   normal `FunctionScope` with `TypeRef` locals instead of passing a private
   callback pair.
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
   `TypeRef` shape and `type_ref_head_name`. Frontend tests now assert emitted
   local inference through returned `TypeRef` nodes instead of a test-only
   string-returning helper.
   Receiver template argument extraction now accepts parsed `TypeRef` only; the
   unused string overload that reparsed rendered receiver types has been
   removed.
   Built-in `min`/`max` and contextual numeric binary inference now pass
   inferred `TypeRef` operands into assignment compatibility instead of calling
   through rendered argument-type strings. Generic inference no longer exposes
   a string-returning template-argument label helper; diagnostics render
   template argument labels locally from parsed `TypeRef` nodes.
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
   Native operator lookup now derives candidate namespaces from the parsed
   receiver `TypeRef` head rather than rendering the receiver type and slicing
   the rendered spelling.
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
   structured `FunctionSignature` accessors; direct `FunctionSignature` mirror
   access in production code is confined to `sema_function_type.cpp`.
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
   Native function declarations now name imported spelling fields explicitly:
   `param_native_spellings` and `return_native_spelling`. They also expose
   structured accessor helpers for parameter and return `TypeRef`s plus
   rendered display text. Symbol collection, native function deduplication, and
   LSP symbol details use those accessors instead of open-coding native
   spelling fallback logic.
   Native type aliases and values now expose structured accessor helpers as
   well. Their raw imported spelling is explicitly named `native_spelling`, and
   symbol collection plus LSP native symbol/local context code use the accessors
   instead of open-coding `type_ref`-or-rendered-string fallback logic.
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
   Assignment compatibility now detects whether native normalization changed a
   type through `TypeRef` equivalence instead of rendering the original type
   and comparing text.
   Normal `new[T]`/`malloc[T]` allocation inference now exposes only the
   parsed `TypeRef` result API; the obsolete string-returning allocation helper
   has been removed. The remaining text allocation helper is confined to the
   explicit `cpp(...)` escape boundary.
   Native overload template matching no longer retries failed argument binding
   through a rendered-string template binder. The structured binder now handles
   native C++ pack-expansion artifacts inside template types, and the old
   string binder API has been deleted.
   Explicit native template call substitution now has a parsed `TypeRef`
   argument overload. The callee-string entry point converts explicit template
   arguments to `TypeRef`s once at the boundary, and the obsolete text binding
   map has been deleted.
   Native variadic pack bindings now store `TypeRef` argument nodes during
   overload matching and render only when feeding the remaining native-artifact
   text replacement boundary.
   Ordinary native template placeholder bindings now store matched `TypeRef`
   nodes instead of rendered type strings. Binding equality checks compare
   structured types first and render only for native artifact compatibility.
   Native overload matching now detects variadic template pack parameters from
   the signature parameter `TypeRef`, keeping raw spelling fallback inside the
   native template helper for malformed C++ artifact nodes.
   Native template substitution now decides whether structured substitution is
   safe by walking signature and binding `TypeRef` nodes, consulting raw node
   spelling only for native artifact markers instead of rendering whole
   signature types first.
   Native explicit-template placeholder discovery now walks signature
   `TypeRef` nodes and scans each node's attached raw spelling for native
   artifact placeholders, instead of rendering whole signature return/parameter
   types before scanning.
   The structured native template substitution branch now assumes the
   already-verified signature `TypeRef` nodes are present and deletes its dead
   render/reparse fallback for missing parameter or return types. Text
   replacement remains only in the explicit native-artifact fallback branch.
   Dudu-origin imported function aliases now populate native alias signatures
   with `TypeRef` parameter and return nodes only, without also materializing
   rendered `params` or `return_type` string mirrors. C++ header imports keep
   their native text mirrors at the foreign boundary.
   Dudu-origin imported type aliases, enum/class aliases, and constant aliases
   now also keep native alias metadata in structured `TypeRef` fields without
   materializing rendered native `type` mirrors. Symbol collection and LSP
   display treat `TypeRef` presence as the alias signal while C++ header imports
   may still use native text mirrors at the foreign boundary.
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
   for plain callees instead of reparsing the callee string. Raw type-spelling
   recognition is now a private helper inside explicit `cpp(...)` escape
   inference.
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
   The unused string-map `substitute_type_ref` overload has been deleted from
   the shared AST type API; native/template paths now use typed substitution
   unless they are explicitly handling complex native fallback text.
   Native header prefixing now builds direct named `TypeRef` metadata for simple
   scanned type names instead of reparsing those names after prefix adjustment.
   Native function return fallback now constructs `auto` directly as a `TypeRef`
   when no native return spelling exists instead of parsing synthesized text.
   Parsed template member calls now pass their `template_type_args` through typed
   method-signature lookup instead of reconstructing `method[T]` strings and
   reparsing the type arguments in sema.
   Core type helpers no longer repair malformed pointer/reference/fixed-array
   `TypeRef` nodes by reparsing `TypeRef.text`; callers and tests must build
   those type shapes with structured child nodes.
   C++ lowering of `TypeRef` pointer/reference/wrapper/fixed-array nodes no
   longer reparses `TypeRef.text` to repair malformed internal nodes. Structured
   lowering expects structured children; the string lowering API remains the
   explicit boundary for raw native spellings.
   Method signature lookup no longer parses bracketed `method[T]` strings.
   Explicit generic method calls must pass parsed `TypeRef` template arguments
   through the typed lookup overloads.
   Native metadata accessor fallbacks no longer parse mirror strings into
   structured `TypeRef` nodes. Header scanning/import code is responsible for
   carrying structured metadata; missing native metadata degrades to `auto` or a
   simple named type instead of hidden reparsing.
   Pointer-cast C++ emission now uses the parsed `Expr::type_ref` carried by the
   pointer-cast call node instead of reparsing the callee spelling during
   codegen.
   Raw type-spelling recognition for `cpp(...)` calls is now private to the
   explicit C++ escape inference boundary; general semantic context exposes only
   structured `TypeRef` type checks.
   C++ artifact normalization now exposes a `TypeRef`-returning path for parsed
   types. Assignment compatibility uses that structured path, so normal
   expected/got type checks no longer render normalized `TypeRef`s and reparse
   them; only the foreign spelling overload still parses raw C++ text.
   C++ type emission now rejects malformed structured pointer, reference,
   wrapper, and fixed-array `TypeRef` nodes instead of falling back to
   `TypeRef.text`. The raw string type-lowering overload remains the explicit
   boundary for imported/native spellings, while structured lowering requires
   structured children.
   Type compatibility no longer calls the type parser at all. The final
   text-comparison compatibility helper now receives already-normalized
   `TypeRef` nodes from its caller and uses those for structural checks instead
   of reparsing normalized expected/got strings.
   C++ expression emission and build-flag integer evaluation now require parsed
   literal value fields for boolean and numeric literals instead of falling
   back to raw expression text. Raw literal spelling remains available for LSP
   display and string literal emission only.
   LSP scope lints now discover locals from structured statement forms that
   sema treats as bindings: typed declarations, first-assignment name targets,
   tuple destructuring, loop bindings, and catch bindings. Unused/shadow
   diagnostics no longer depend only on `VarDecl` statements.
   LSP symbol-at-cursor and reference collection now use the same structured
   binding forms for assignment-created locals and tuple destructuring targets.
   Find-references/rename no longer treats inferred local definitions as plain
   expression text while only typed declarations count as bindings.
   Reference collection now has its own API header instead of being declared by
   the generic navigation header, keeping the LSP ownership boundary clearer as
   the remaining selection/reference helpers move onto shared AST state.
   Reference collection also exposes a parsed-`ModuleAst` entry point, so
   reference and rename requests can reuse modules they have already parsed
   instead of making collection parse source text internally.
   LSP reference and rename scope checks now distinguish project-visible
   symbols from the current file's visible symbols, so imported module aliases,
   target declarations, and same-file redeclaration filtering do not share a
   vague document-symbol query. Member-call selection also preserves full
   callee paths such as `module_alias.function` when the cursor is on the
   member name. References and rename now derive cursor selection from the same
   visible parsed module unit used for their symbol and reference scans, and a
   misleading unused module/document symbol overload was removed. Definition
   lookup now also reuses one loaded module tree for header imports, cursor
   selection, current-file symbols, and Dudu import resolution before loading
   native-aware state only for member/native fallbacks. Hover lookup now uses
   the same loaded module tree for normal/imported symbols and loads
   native-aware state only for native symbol/member fallback hover. The public
   LSP symbol collection API no longer parses documents internally; document
   symbols, workspace symbols, completions, missing-import quick fixes, and
   native-reference checks now load or parse modules explicitly before asking
   the symbol layer to collect declarations. Document-parsing LSP cursor
   selection wrappers were also removed; hover derives cursor selection from
   the same visible module unit it already loaded. Completion and signature
   help now also reuse one loaded module tree per request instead of loading
   again inside module/member/symbol helpers. LSP local-type lookup now
   consumes a loaded visible module unit instead of parsing the document inside
   local-context helpers. Semantic-token requests also load the same visible
   module unit through the shared module loader, with native-aware state loaded
   only for native token classification instead of open-coding parse plus
   header merge in the request handler. LSP code actions now also load modules
   through the shared module loader and operate on visible current/candidate
   units for organize-import and missing-import actions instead of carrying a
   private document parser wrapper.
   `offsetof[Type]("field")` C++ emission now requires the parsed string
   literal value carried by the AST instead of unquoting raw expression text as
   a malformed-node fallback.
   LSP member completion/definition candidate discovery now accepts the
   receiver's structured `TypeRef` directly and expands Dudu/native aliases
   from typed metadata instead of requiring callers to render the receiver type
   into a string first.
   Class `static[...]` field parsing now rejects malformed static type nodes
   that are missing a child type instead of rendering the static wrapper back
   into an `Unknown` compatibility type.
   Declaration type parsing now rejects malformed `TypeRef` trees with parser
   diagnostics instead of letting `Unknown` type nodes with raw source text
   survive into declaration ASTs.
   Semantic base-type discovery no longer recovers malformed pointer,
   reference, or fixed-array `TypeRef` nodes from `TypeRef.text`; those
   structured nodes must carry children or produce no base type.
   Member-call semantic inference now checks missing/`auto` receivers through
   structured `TypeRef` state instead of rendering the receiver type to text
   just to compare against `"auto"`.
   Member-field semantic inference now does the same for direct member
   expressions and only renders receiver types when constructing a diagnostic.
   Statement semantic checking no longer renders the function return type for
   every statement; return checks use structured void predicates and render
   type names only for diagnostics.
   Native enum-value assignability now checks integer expected types through
   structured `TypeRef` metadata instead of rendering the expected type before
   testing whether the enum value can be assigned.
   Match semantic checks now validate guard expressions with structured bool
   `TypeRef` predicates and only render guard or subject types when producing
   diagnostics.
   Assignment mismatch diagnostics now accept structured `TypeRef` values for
   the inferred type, so semantic body checks no longer render the got type just
   to hand it to the diagnostic helper.
   Builtin `min`/`max` semantic checks now defer argument type rendering until
   the diagnostic branch instead of rendering every checked argument eagerly.
   Constructor argument semantic checks now also defer inferred argument type
   rendering until a constructor mismatch diagnostic is actually emitted.
   `@extern_c` ABI checking now classifies pointer-to-`struct`/`union`/`enum`
   types from parsed `TypeRef` head names instead of rendering child types and
   checking source-text prefixes.
   Structured `new[T]`/`malloc[T]` allocation inference no longer renders the
   allocated type before validation; it renders only when emitting an abstract
   allocation diagnostic.
   Builtin C++ method signature inference now recognizes parsed template heads
   for `std.vector`, `std.map`, `std.optional`, `std.atomic`, and related
   containers instead of rendering the receiver type and searching for C++
   spelling fragments.
   Pointer-cast C++ emission no longer has a raw `TypeRef.text` special case
   for `struct Foo` cast targets; it recognizes the parsed type shape carried
   by the call expression.
   Inferred fixed-array literal types no longer synthesize a rendered
   `array[T][shape]` mirror into `TypeRef.text`; the inferred type is carried
   by `FixedArray` children and shape value metadata, and renderers derive the
   spelling from that structure.
   Receiver class lookup no longer renders a receiver `TypeRef` when the
   structured head name is missing; member lookup uses parsed receiver type
   structure and C-tag stripping only.
   Structural type compatibility now compares parsed head names and value
   fields for named/template/value nodes instead of falling back to raw
   `TypeRef.text`; raw text comparison remains confined to `Unknown` nodes.
   Native template placeholder binding now reads parsed `TypeRef` head/value
   metadata through `type_ref_head_name` instead of falling back to raw
   `TypeRef.text` for named/template/value placeholders.
   Numeric native template arguments now build structured `Value` `TypeRef`
   nodes with `value` metadata only, without duplicating the number into
   `TypeRef.text`.
   Core `type_ref_equivalent` now compares parsed head names and value fields
   for named/template/value nodes instead of falling back to raw
   `TypeRef.text`; only `Unknown` nodes compare raw text.
   `type_ref_head_name` and `substitute_type_ref_text` now render
   named/qualified/template/value nodes from parsed `name` or `value` fields
   only, leaving raw text rendering to `Unknown` and malformed wrapper
   fallback cases.
   Native signature placeholder discovery and structured-substitution guards
   now scan raw `TypeRef.text` only for explicit `Unknown` native-boundary
   nodes; structured type nodes recurse through parsed head/value/child fields.
   C++ associated-type compatibility now checks parsed `TypeRef` head names for
   iterator/value_type-style aliases instead of rendering whole type nodes for
   suffix matching.
   Native overload numeric-promotion matching now uses parsed parameter/argument
   `TypeRef` head names only; rendered strings remain only in mismatch
   diagnostics.
   Pointer-template casts now carry the parsed target type on the call
   expression, and sema/codegen reuse that `TypeRef` instead of stripping `*`
   from the callee spelling and rebuilding the target from text.
   Parsed `cpp(...)` allocation expressions such as `new[T]` and `malloc[T]`
   now infer through the parsed `TemplateCall` and its `TypeRef` arguments;
   the duplicate raw string allocation inference fallback has been deleted.
   Parsed `cpp(...)` pointer casts now reuse the parser-attached target
   `TypeRef` instead of slicing the callee spelling to recover the type.
   Parsed `cpp(...)` calls now carry a structured callee `TypeRef`, so
   constructor/type-name checks can use parsed name/template data instead of
   reparsing the callee spelling.
   `cpp(...)` expression type inference now uses `infer_cpp_escape_expr_ref`
   as the primary structured path; the legacy string-returning helper renders
   the resulting `TypeRef` instead of forcing sema to infer text and reparse it.
   Native explicit template matching now parses explicit template arguments
   once into `TypeRef` nodes at the call boundary and uses the structured
   substitution overload; the duplicate string-argument substitution overload
   has been deleted.
   Explicit native template substitution now requires structured parameter and
   return `TypeRef` metadata instead of falling back to signature text
   replacement and reparsing.
   Bound native template substitution now expands direct variadic pack
   parameters such as `T...` from `TypeRef` pack bindings before falling back
   to native signature text for compiler-specific artifacts.
   Bound native template substitution is now per-field rather than
   all-or-nothing: structured parameters and returns stay on `TypeRef`
   substitution even when a different field still needs native artifact
   fallback text. Native template pack-placeholder detection exposes only the
   parsed `TypeRef` API outside its implementation file; the string spelling
   helper is private to the native-template boundary.
   Explicit cast assignment compatibility now compares the parsed call target
   as a normalized `TypeRef` instead of rendering the expected type and
   comparing it to callee spelling.
   The suspicious numeric-cast lint now classifies source and target numeric
   types through `TypeRef` head names, rendering only the final diagnostic
   message.
   LSP semantic type-token collection now uses `TypeRef` helper APIs for type
   heads and rendered spellings instead of reading `name`/`text` fields
   directly.
   LSP semantic native type/class token classification now checks parsed
   `TypeRef` head names directly instead of rendering whole template type
   spellings for native-index lookup.
   LSP member-candidate type expansion now uses parsed `TypeRef` head names
   and C-tag-stripped head aliases instead of adding whole rendered type
   spellings to the candidate set.
   Explicit native template non-type argument binding now detects numeric
   value arguments through `TypeRef::Value` metadata instead of rendering
   template arguments to strings first.
   String compatibility now classifies normalized `TypeRef` heads instead of
   rendering normalized type strings for the `str` / `std.string` /
   `std::string` equivalence rule.
   Final assignment compatibility now treats `auto`, missing RHS type, C string
   literal compatibility, and normal structured type equivalence through
   `TypeRef` metadata first; rendered type equality is restricted to missing or
   explicit `Unknown` native-boundary nodes.
   Numeric literal assignment compatibility now checks `ExprKind` directly
   instead of comparing a rendered literal category string such as `number`.
   C++ swizzle emission no longer guesses local Dudu class receivers by
   rendering a receiver type and checking uppercase spelling; it resolves the
   receiver `TypeRef` through `Symbols` and checks known non-native classes
   directly.
   Class definition emission order now walks field and base-class `TypeRef`
   trees to find dependencies instead of rendering field types and doing
   substring class-name searches.
   Class codegen now resolves base-class lookup through structured `TypeRef`
   heads for polymorphic parent checks, and emits `super` call scopes by
   lowering the base `TypeRef` at the C++ spelling boundary instead of storing
   rendered Dudu type text.
   Emission-side receiver-base inference now unwraps structured wrapper
   `TypeRef` nodes only; malformed wrapper nodes no longer render fallback type
   strings to participate in method return lookup.
   Emission-side indexed local inference now labels array index diagnostics
   from parsed `TypeRef` head metadata instead of rendering the full receiver
   type during type inference.
   Emission-side call type inference now uses `Symbols` to recognize known
   Dudu/native type constructors in real compiler paths; the old uppercase-name
   constructor heuristic is limited to explicit symbol-less helper calls.
   Duplicate base-class validation now resolves aliases and compares
   structured `TypeRef` trees; rendered base text is only used for the final
   diagnostic label.
   Inheritance ambiguity and abstract-method tracking now build method
   identities from function names plus alias-normalized `FunctionSignature`
   `TypeRef`s; rendered signatures are labels only, not semantic map keys.
   Match emission no longer renders the subject `TypeRef` into an unused text
   mirror before wrapper/enum dispatch; codegen uses the structured
   `TypeRef` directly.
   Type assignment compatibility now keeps normalized assignment checks on
   structured `TypeRef`s and no longer treats two raw `Unknown` type spellings
   as compatible; malformed type nodes must be rejected or modeled
   structurally instead of accepted by rendered text comparison.
   C++ escape type recognition now detects function types from structured
   `TypeRef` nodes instead of checking whether the raw callee spelling starts
   with `fn(`.
   Fast validation now includes `scripts/check_ast_migration_guards.sh`, which
   rejects reintroducing the old `statement_from_text` path or raw statement
   semantic fields under `src/dudu`.
   Emitted expression type inference no longer guesses symbol-less constructor
   calls from uppercase spelling; constructor inference now requires real
   `Symbols` class/native/type metadata.
   Member expression emission no longer guesses `.` versus `::` from uppercase
   receiver spelling; class static access, enum variants, native classes, native
   prefixes, and module prefixes are selected from symbol metadata.
   Pointer-cast emission no longer accepts named pointee types based on uppercase
   spelling; named cast targets must resolve through real symbol/type metadata.
   The fast AST migration guard now also rejects reintroducing the deleted
   uppercase constructor spelling heuristic or the legacy alias-resolution
   fallback helper.
   C++ associated-type assignment matching now uses structured `TypeRef` head
   names only; missing/unknown raw type text no longer participates by rendered
   fallback.
   Native C++ artifact normalization now exposes only the structured
   `normalize_cpp_type_artifacts_ref` API. The unused string-returning
   `normalize_cpp_type_artifacts(TypeRef)` wrapper and its rendered pointer-cv
   cleanup path have been deleted and guarded.
   Wrapper match metadata no longer stores rendered template argument mirrors;
   Option/Result pattern binding uses the parsed `TypeRef` argument list only.
   Native template placeholder rebinding no longer accepts matching rendered
   type strings as semantic equality; repeated bindings compare normalized
   structured `TypeRef`s and structural compatibility instead.
   `base_type`/`known_type_ref` no longer treat non-empty `Unknown.text` as a
   valid type name; empty `Unknown` remains the missing-type sentinel, while
   malformed type nodes must be rejected explicitly.
   C++ type lowering from structured `TypeRef` nodes now rejects `Unknown`
   nodes instead of lowering their raw text through the string type lowerer.
   Generic/type alias substitution no longer treats raw `Unknown.text` as a
   placeholder lookup key; substitutions now require structured named,
   qualified, or template type nodes, so malformed unknown type text cannot
   quietly participate in semantic rewriting.
   Native variadic template parameters now use a structured
   `TypeKind::PackExpansion` node instead of mutating `TypeRef.text` to hold
   spellings like `T...`; native header scan normalizes trailing and nested
   C++ template ellipsis at the import boundary, and signature expansion
   consumes the structured pack node directly. The old nested pack matcher
   that manufactured an `Unknown.text` node containing a joined type list has
   been removed.
   Bare C variadic `...` parameters in native overload matching are also
   recognized from the `TypeKind::PackExpansion` shape instead of rendering the
   parameter to text and comparing against `"..."`; the migration guard rejects
   reintroducing that rendered check. Native template substitution also passes
   bare pack parameters through structurally instead of rendering them through
   rendered signature parameter text.
   Native template placeholder discovery now walks structured `TypeRef` heads,
   values, and children only; it no longer scans raw `Unknown.text` for names
   such as `T`, so malformed native type text cannot create implicit template
   bindings.
   Native template binding classification no longer treats selected
   `Unknown.text` spellings as structured bindings; unknown native binding
   values now stay on the explicit native-artifact fallback path instead of
   entering structured substitution.
   Assignment compatibility no longer has a rendered-string equality fallback
   for missing type metadata; compatibility is decided by structured `TypeRef`
   rules, explicit missing-type rules, literals, and diagnostics may still
   render type labels for messages.
   Assignment diagnostic helpers now also expose only the structured
   `assignment_error(TypeRef, Expr, TypeRef)` API. The old string overload used
   by tests to pass an empty "got type" display value has been deleted, and the
   AST migration guard rejects reintroducing it.
   Native header parsing now routes aliases, base classes, C++ methods,
   constructors, fields, enum constants, and globals through the same
   native-type normalizer used for free functions, so imported declaration
   metadata receives structured pack-expansion and nested native type cleanup
   at the boundary instead of leaving direct raw `parse_type_text` holes.
   Native header type cleanup now recursively normalizes parsed child
   `TypeRef` nodes directly; the old `parse_native_type_text(type_ref_text(...))`
   render/reparse loop has been deleted and guarded.
   Bound native template substitution now actually applies structured
   substitution per parameter/return field: a messy unrelated native binding
   no longer disables `TypeRef` substitution or pack expansion for clean
   fields in the same overload. The remaining text replacement is reserved for
   the specific field that needs native-artifact fallback.
   Native explicit-template calls now pass parsed `TypeRef` template arguments
   from `TemplateCall` expressions into overload matching directly. The old
   `native_template_call_base` path decoded `std.get[0]`-style arguments by
   reparsing the rendered callee string; that parser has been deleted and the
   migration guard rejects reintroducing it.
   The now-unused `native_template_binding_type_ref` helper has also been
   deleted. Tests that need messy native type metadata build the same boundary
   shape with `parse_type_text`, while live native template matching consumes
   `TypeRef` arguments produced by parsing/scanning rather than strings.
   Native header class merging now dedupes imported base classes with
   `type_ref_equivalent` over parsed `TypeRef` nodes. The previous merge key
   rendered base types to strings and inserted them into a set.
   Native function and C++ method dedupe now compare names, arity, variadic
   metadata, return `TypeRef`, and parameter `TypeRef` nodes structurally. The
   old rendered `native_function_key` and `method_key` helpers have been
   deleted and guarded.
   The dead rendered `template_method_name` helper has been deleted as well;
   template method sema now uses parsed member names and `TypeRef` template
   argument lists directly.
   C++ type lowering from structured named and qualified `TypeRef` nodes now
   goes through a name-only lowering helper instead of rendering the type name
   and sending it back through the raw string type parser. Raw type-string
   lowering remains only for explicit native/C++ escape compatibility paths
   and tests that exercise that boundary.
   Native header merge now uses `NativeSymbolId` for types, values, macros,
   namespaces, and native class shapes when deciding whether a repeated Dudu
   binding is the same native declaration. Unqualified same-name/different-
   identity imports are rejected instead of being silently deduped by spelling;
   scanner-collapsed C++ system-header artifacts such as `_Up`, `iterator`,
   `value_type`, and `type` remain tracked by the native identity plan.
   Native scan dedupe now applies the same identity-aware rule before merge, so
   scanner output cannot lose unqualified same-name/different-identity
   declarations through a name-only set.
   The AST migration guard now rejects `lower_cpp_type(type_ref_head_name(...))`
   so structured type-name codegen cannot silently re-enter the raw type parser.
   Raw string C++ type lowering no longer has its own hand-written function
   type splitter for `fn(...)` spellings. Those spellings now go through
   `parse_type_text` and structured `TypeRef` function lowering, with a guard
   preventing the deleted `lower_function_signature_type` helper from coming
   back.
   Raw string C++ type lowering also no longer peels pointer/reference/const
   wrapper spellings with `type.substr(...)`; those wrapper spellings are parsed
   into `TypeRef` nodes and lowered through the structured path. The guard
   rejects reintroducing recursive substring wrapper lowering.
   Raw C++ template-argument lowering now shares the same structured function
   `TypeRef` lowering for `fn(...)` spellings, preserving non-pointer function
   signatures for template arguments without a second hand-written parser.
   Raw known Dudu template spellings such as `list[...]`, `dict[...]`,
   `tuple[...]`, `variant[...]`, `std.function[...]`, and related built-in
   template roots now parse into `TypeRef` and use the structured type lowerer
   instead of duplicating the built-in template semantics with raw bracket
   splitting.
   The remaining raw bracket-template spelling path now also parses arbitrary
   `Name[...]` type strings into structured `TypeRef` nodes, so
   `Box[list[i32]]` shares the same template lowering as parsed source. The old raw
   `lower_template_arg_type` and `lower_template_type(std::string_view, ...)`
   helpers are guarded against reintroduction.
   Legacy fixed-array shorthand spellings such as `Player[3][4]`,
   `Box[list[i32]][3]`, and `Pixel[BUFFER_SIZE]` are not Dudu syntax. The
   parser only forms fixed arrays from canonical `array[T][shape]`, and the
   previous `find/substr` helpers for `fixed_array_dimensions` and
   `fixed_array_base` are guarded against reintroduction.
   `raymarch-dd` dogfood found a Release-only `TypeRef` lifetime bug in
   wrapped index receiver unwrapping: replacing a wrapper with
   `type.children.front()` can copy from storage owned by the value being
   overwritten. Wrapper peeling must copy the child first and then replace the
   parent, and `reference_array_index.dd` guards the indexed-reference path.
   The dead `parse_unknown_until_stops` helper has been deleted, and the
   remaining call parser that consumes raw text is named as an explicit
   `cpp(...)` escape-boundary helper rather than a generic expression fallback.
   LSP reference query selection no longer scans the current source line for
   dotted text; it relies on parsed AST symbols and expression paths, preserving
   the existing string/comment false-positive guards. The leftover dotted-symbol
   character helper from that scanner was also deleted.
   Dudu constructor/destructor method-name predicates now live in the naming
   module instead of being duplicated in declaration sema and C++ class
   emission.
