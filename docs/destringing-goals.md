# Destringing Goals

Dudu should use strings for names, literal values, operator spellings, source
text display, explicit `cpp(...)` escape payloads, and native C/C++ boundary
metadata. It should not use raw source snippets as semantic AST payload or as a
parallel compiler path.

These goals are ordered. Each goal should leave the repo green, add guards
where practical, and remove old paths instead of preserving compatibility.
Dudu is unreleased, so accidental accepted syntax and prototype internals should
be deleted.

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
