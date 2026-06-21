# Destringing Goals

Dudu should use strings for names, literal values, operator spellings, source
text display, explicit `cpp(...)` escape payloads, and native C/C++ boundary
metadata. It should not use raw source snippets as semantic AST payload or as a
parallel compiler path.

These goals are ordered. Each goal should leave the repo green, add guards
where practical, and remove old paths instead of preserving compatibility.
Dudu is unreleased, so accidental accepted syntax and prototype internals should
be deleted.

## Meta Goal

Use this as the execution goal for the whole destringing slice:

```text
Execute Coding/GameDev/dudu/docs/destringing-goals.md in order until all six
goals are complete. Treat this as the top-priority architecture slice of
Coding/GameDev/dudu/docs/le_plan.md: get Dudu off raw source/string semantics
and onto structured AST, semantic facts, module metadata, and explicitly named
native-boundary metadata. Do not preserve compatibility syntax or fallback
compiler paths; Dudu has no released users. Strings are allowed only for
identifiers, literal values, operator spellings, source display, diagnostics,
formatter trivia, explicit cpp(...) escape payloads, emitted C++, and native
C/C++ boundary metadata whose field names make that boundary explicit.

For each goal, delete the old string path, add focused guards/tests that prevent
it from coming back, keep validation fast, commit and push stable green
milestones, and update this document when implementation discoveries tighten the
spec. Avoid getting stuck behind one slow or hanging validation case; isolate
slow tests from the fast loop and keep moving on the architectural goal. When
all six goals are complete, resume Coding/GameDev/dudu/docs/le_plan.md.
```

## Goal 1: Lock Statement AST To Structured Nodes

Objective:

- Make parsed Dudu statements fully structured.
- Keep `Stmt` free of raw expression/type source snippets.
- Ensure sema, codegen, lints, formatter, LSP local context, and test discovery
  consume statement fields such as `target_expr`, `value_expr`,
  `condition_expr`, `type_ref`, and `children`.

Allowed strings:

- declaration names
- C++ escape block lines
- diagnostic display text generated from AST when needed

Definition of done:

- no `statement_from_text`
- no raw `Stmt::value`, `Stmt::target`, `Stmt::condition`, or similar mirrors
- no substring-based statement kind classification after token parsing
- migration guard rejects reintroducing those names/helpers
- negative tests cover unsupported statement forms with useful diagnostics

Status: complete. `Stmt` carries structured expression/type fields such as
`target_expr`, `value_expr`, `condition_expr`, `message_expr`, `iterable_expr`,
`pattern_expr`, `guard_expr`, `type_ref`, and `children`. The remaining
statement string storage is `cpp_lines`, which is the explicit C++ escape block
payload. `scripts/check_ast_migration_guards.sh` rejects reintroducing
`statement_from_text`, `Stmt::value`, `Stmt::target`, `Stmt::condition`, and
`Stmt::return_type`. Unsupported statement fixtures cover deliberately rejected
Python statement forms.

## Goal 2: Remove Raw Source Payload From Normal Expressions

Objective:

- Remove raw source snippet dependence from normal parsed `Expr` nodes.
- `Expr.text` must not be used as semantic payload for valid Dudu expressions.
- Display, diagnostics, LSP token lengths, and codegen should derive from
  structured expression nodes and source ranges.

Allowed strings:

- `Expr.name` for identifiers, members, named arguments, and similar names
- `Expr.value` for literal values
- `Expr.op` for operators
- explicit `cpp(...)` escape payloads
- unsupported-expression display only as a diagnostic aid, not semantic input

Definition of done:

- normal expression parser paths do not fill raw source snippets for valid
  expressions
- no sema/codegen/lint/LSP path reads `Expr.text` for valid Dudu expression
  behavior
- string literal semantic tokens use ranges or literal value metadata, not raw
  source snippet payload
- unsupported Python-like expressions carry structured kind/range and produce
  targeted diagnostics
- migration guard rejects new `expr.text` semantic reads outside explicitly
  named display/escape/unsupported helpers

Status: complete. `Expr` no longer has a raw source payload field. Parsed
expressions carry names, literal values, operators, children, and source ranges
instead. String literal semantic tokens use source ranges, `cpp(...)` escape
payloads come from the parsed string literal value, and display text is rendered
from structured expression nodes. `scripts/check_ast_migration_guards.sh`
rejects reintroducing `expr.text` reads in compiler/test sources.

## Goal 3: Remove Raw Source Payload From Dudu Type AST

Objective:

- Remove `TypeRef.text` dependence for parsed Dudu source types.
- `TypeRef` should represent named, qualified, template, pointer, reference,
  const, function, fixed-array, value, and pack-expansion types structurally.

Allowed strings:

- type names
- template/value argument spellings where the argument is actually a value
- native C/C++ original spelling in native-boundary metadata
- display text rendered from structured `TypeRef`

Definition of done:

- parsed Dudu source types never require `TypeRef.text` to be meaningful
- `Unknown` does not silently preserve a raw type spelling as a usable type
- `type_ref_text` renders from structure only for Dudu types
- sema/codegen/lints/LSP do not reparse `TypeRef.text`
- migration guard rejects raw `TypeRef.text` semantic reads outside native or
  display helpers

Status: complete. `TypeRef` no longer has a raw source payload field. Parsed
Dudu types carry structured names, values, children, ranges, and a `malformed`
flag that distinguishes bad type syntax from a missing type annotation.
Malformed delimiter syntax such as `Bad[` now produces a malformed type node
instead of a string-backed unknown type. Suffix pack expansion such as `T...`
parses as `TypeKind::PackExpansion`, which removed a native variadic-template
dependency on child raw text. Native header parsing now drops impossible bare
wrapper artifacts such as childless `const` rather than letting them escape into
normal Dudu type semantics. The migration guard rejects reintroducing
`TypeRef.text` and field-style type text reads.

## Goal 4: Isolate Native C/C++ Spelling Metadata

Objective:

- Move raw native spelling to explicitly named native-boundary fields.
- Normal Dudu AST, sema, and codegen should not treat raw C/C++ spelling as
  ordinary Dudu type identity.

Allowed strings:

- original C/C++ spelling in native metadata
- macro spelling and macro replacement metadata
- emitted C++ text after structured lowering

Definition of done:

- native declarations carry both structured `TypeRef` metadata and explicit
  native spelling fields when needed
- native identity work is wired to the
  [Native Identity Plan](native-identity-plan.md)
- raw spelling comparisons are removed from ordinary type compatibility when
  structured/native identity metadata exists
- suffix-name native compatibility heuristics are replaced or quarantined behind
  clearly named native-boundary helpers
- tests cover direct imports, aliased imports, typedefs, namespaces with same
  tail names, and inline namespace artifacts

Status: complete. Native declaration string fields have been renamed to
make the boundary explicit: `NativeTypeDecl::native_spelling`,
`NativeValueDecl::native_spelling`,
`NativeFunctionDecl::param_native_spellings`, and
`NativeFunctionDecl::return_native_spelling`. Call sites now use structured
`TypeRef` metadata first and render native display/detail text through the
native declaration accessors. The AST migration guard rejects reintroducing the
old ambiguous native declaration fields `type`, `params`, and `return_type`.
`NativeSymbolId` is now present on native type, value, function, macro, and
namespace declarations. Header scanning populates `identity.canonical_path`,
and aliased header imports preserve that canonical identity while adding the
user-facing prefixed binding. The scanner also treats inline namespaces as
transparent instead of importing `inline` as a bogus namespace component.
C++ associated-type suffix matching for imported native templates, such as
`iterator`, `const_iterator`, `value_type`, and `size_type`, now lives behind
the explicit native-boundary helper
`native_associated_type_assignment_allowed` instead of ordinary type
compatibility owning that spelling rule directly. C++ associated operand
loosening for operator sema, such as `.reference` and `.iterator`, now lives
behind `native_associated_operator_operand_is_dependent`; the guard rejects
putting that suffix check back into `sema_ops.cpp`. Direct, aliased, typedef,
same-tail namespace, and inline namespace scanner cases are covered by native
frontend tests. Deeper USR collection and identity-aware map keys remain in the
[Native Identity Plan](native-identity-plan.md), but the raw-spelling boundary
required for this destringing goal is now explicit and guarded.

## Goal 5: Delete String Fallback APIs And Add Final Guards

Objective:

- Remove compiler-internal APIs that accept rendered source/type/expression
  strings as fallback inputs after structured AST equivalents exist.
- Keep raw string APIs only for explicit source text parsing entry points,
  display, native scanner input, and `cpp(...)`.

Definition of done:

- no semantic assignability hook takes rendered source/type strings when
  `Expr`/`TypeRef` data is available
- no codegen path reparses already-tokenized Dudu source
- no LSP/lint/formatter path performs private raw expression/type parsing for
  normal Dudu files
- final guard script rejects known deleted helpers and raw semantic fields
- fast validation plus representative native/library probes pass
- docs describe the remaining legitimate string boundaries

Status: complete. Prototype-era raw `sema_scan` helpers for compound
assignment, top-level logical detection, top-level comparison detection, and
comparison text extraction have been deleted. Those forms are parsed through
structured statement/expression AST now. The migration guard rejects
reintroducing `compound_assign_pos`, `find_top_level_logical`,
`find_top_level_comparison`, and `top_level_comparison_text`. The remaining
`sema_scan` helpers are still used by explicit `cpp(...)` escape parsing and
local member-path segment validation, so they stay in the allowed string
boundary. The string overloads of `lower_cpp_type` have also been
removed; normal codegen must lower structured `TypeRef` values, while
`cpp(...)` escape rewriting and explicit spelling tests use
`lower_cpp_type_spelling` to make the raw type-text boundary visible at the
call site. The guard rejects reintroducing `lower_cpp_type(std::string)` or
`lower_cpp_type(const std::string&)`, and also confines
`lower_cpp_type_spelling` callers to the explicit C++ escape/native spelling
boundary files. Raw string integer predicates in
`sema_ops` have also been removed; semantic integer checks now use
`type_ref_is_integer(const TypeRef&)`, and the guard rejects the old
`is_integer_type(type_ref_head_name(...))` shape. The unused string overload of
`normalize_cpp_type_artifacts` was deleted as well, leaving the native artifact
normalizer on structured `TypeRef` input only. The string-returning
`infer_cpp_escape_expr` test wrapper was removed; explicit `cpp(...)` escape
inference exposes `infer_cpp_escape_expr_ref` and display rendering happens only
at diagnostics or tests. Raw template-call argument lowering for `cpp(...)`
escape rewriting has been renamed to `lower_raw_template_call_arg` so ordinary
template lowering cannot accidentally accept text. Native variadic-template
matching now detects pack parameters structurally, accounts for fixed
parameters before a pack, binds placeholder types before assignability checks
short-circuit, and expands nested pack use in return types such as tuple
factory signatures. Native C++ type-trait artifacts including
`std::remove_reference` and `__decay_and_strip` are collapsed at the native
boundary before structured substitution, and `basic_string[char]` /
`basic_string[i8]` normalize to Dudu `str`. The migration guard,
`probe_cpp_stdlib_algorithms.sh`, `test_codegen_shapes.sh`, and
`test_fast.sh` pass for this milestone. `FunctionSignature` no longer exposes
string-returning parameter or return type helpers; callers use `TypeRef`
accessors and render only at diagnostic/native display edges. The migration
guard rejects reintroducing those public string helpers.

## Goal 6: Separate Module Outputs Without Re-Flattening

Objective:

- Use the structured module graph and public interface metadata to emit
  separate generated files without depending on a merged raw source view.

Definition of done:

- each `.dd` module can generate its own C++ source/header unit where practical
- imports lower to generated includes/references instead of textual flattening
- CMake/Ninja can rebuild changed generated modules independently
- diagnostics still point to original Dudu source ranges
- the merged-output backend is treated as a compatibility backend for narrow
  direct builds, not the semantic model

Status: complete. Source-tree module units are preserved on `ModuleAst` with
resolved dependency metadata. `emit_cpp_module_artifacts` emits a shared runtime
header plus generated `.hpp/.cpp` artifacts for each module unit, and
`duc emit-modules` writes those artifacts to disk. The per-module emitter uses
stable generated declaration names for same-module declarations and qualified
imported module references, so same-named declarations in different Dudu
modules no longer collide in the emitted artifacts. The generated CMake backend
uses `duc emit-modules` and compiles the per-module generated `.cpp` files
instead of compiling one merged Dudu translation unit. The direct backend still
uses the narrow merged-output path and now fails clearly when that path cannot
represent distinct module declarations safely. Regression coverage includes the
module metadata/unit tests, `project_backend_auto_modules`, and the broad
`scripts/test.sh` pass.
