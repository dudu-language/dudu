# AST Plan

Dudu needs a real statement and expression AST. The current compiler has a
real lexer, parser, module AST, class/function/import declarations, and nested
raw statement blocks. That was enough to prove the language shape, C/C++
interop, and generated C++ output. It is not enough for the next level of
errors, generics, macros, semantic highlighting, and reliable refactors.

The goal is not to make the compiler academic. The goal is to stop treating
function bodies as strings.

Dudu does not need a separate low-level optimizer IR as the next step. The
proper next compiler layer is a typed/core AST, or HIR, that preserves Dudu
program structure after name binding, type checking, overload resolution, and
desugaring. C++ codegen should consume that structured HIR directly. A lower
level IR should be introduced only when Dudu needs optimizer passes, non-C++
native backends, deeper dataflow analysis, or another concrete capability that
HIR-to-C++ cannot honestly support.

## Current Shape

Already structured:

- tokens and indentation
- imports
- classes
- fields
- methods
- functions
- parameters
- enums
- constants
- decorators, including AST-backed helper lookup for recognized decorator names
  and first arguments
- native header metadata
- direct parser construction of statement nodes for function and method bodies
- parsed `Stmt` bodies stored on functions and methods
- initial expression shape capture for common names, literals, calls,
  template calls, member/index/slice access, unary/binary operators,
  conditionals, and collection literals
- initial type shape capture for names, qualified names, templates, pointers,
  references, wrappers, fixed arrays, and function-like type signatures
- type parsing now enters through the lexer/token stream instead of the old
  top-level arrow/bracket substring parser, preserving existing `TypeRef`
  shapes while making C tag spellings such as `struct stat` structured names
- parser declaration and statement code now builds expression and type pieces
  through token spans directly; the parser layer no longer calls the top-level
  text expression/type parsers as a compatibility fallback
- expression template-call bracket contents are parsed from existing token
  spans for both expression and type template arguments, including templated
  pointer casts, instead of reconstructing substring arguments and sending
  them back through top-level text parsers
- function pointer/callback signature parsing can consume parsed `TypeRef`
  nodes directly, including omitted-return `fn(...)` as `void` and wrapper
  templates such as `std.function[fn(...)]`
- function pointer/callback signature parsing renders return and parameter
  children through the shared `TypeRef` helper instead of reading raw child
  text directly
- function signatures now preserve parsed parameter and return `TypeRef` nodes
  beside the compatibility strings, and call-argument semantic checks prefer
  those structured expected types when they are available
- Dudu-native method signature instantiation now substitutes class and method
  generics through parsed `TypeRef` nodes and preserves structured parameter
  and return metadata for downstream call checks
- inherited and abstract method signature matching now preserves parsed
  `TypeRef` metadata while substituting generic base-class receiver types
- generic Dudu field lookup now substitutes class template parameters from
  parsed field `TypeRef` nodes before rendering member-access result types
- function pointer/callback omitted-return detection checks for a missing
  parsed return `TypeRef`, not an empty raw child string
- generic argument inference gets candidate parameter and argument names through
  a shared parsed `TypeRef` head-name helper instead of open-coding
  `name`/`text` fallback logic
- generic argument inference stores inferred bindings as `TypeRef` nodes, so
  successful function and method generic inference returns structured type
  arguments instead of reparsing rendered binding text
- generic function and method inference can now request typed expression
  results through `infer_expr_type_ast`, so local/type-aware arguments cross
  that boundary as `TypeRef` nodes before hitting compatibility fallback paths
- body semantic checking now exposes typed expression inference through
  `BodyCheckCallbacks`, so return-context generic method inference and
  statement-level `delete` checks can pass `TypeRef` values through body sema
- implicit local bindings now preserve inferred `TypeRef` metadata when body
  sema has typed expression results available
- deallocation argument checks now accept `TypeRef` nodes, so `delete` and
  `free` validation checks pointer shape structurally instead of parsing
  rendered argument type strings inside the allocator helper
- shared template and unary type child helpers render parsed `TypeRef` children
  structurally instead of returning raw child text
- array shape inference and static field type extraction render parsed
  `TypeRef` children structurally instead of carrying raw child text
- inferred array literal shapes now carry a fixed-array `TypeRef`, so sema and
  C++ emission bind inferred array locals without regenerating `array[T][...]`
  text and reparsing it
- local declaration sema and C++ emission now carry an effective declared
  `TypeRef` for shaped-array inference, so the declaration path no longer
  decides whether parsed type metadata is valid by comparing rendered type
  strings
- LSP local binding now uses the same shaped-array `TypeRef` inference for
  annotated locals, so hover/completion facts follow the compiler's effective
  declaration type instead of the raw annotation string
- typed catch and loop bindings now store local types from parsed `TypeRef`
  metadata in sema, codegen, and LSP local scopes instead of using raw
  annotation strings as the semantic local type
- suspicious-cast lint facts for annotated locals and parameters now render
  from parsed `TypeRef` nodes instead of reading raw annotation strings
- typed local/catch/loop/lint paths now test parsed `TypeRef` presence instead
  of treating non-empty raw annotation text as proof that a semantic type node
  exists
- C ABI pointer checks and structural type-name compatibility compare rendered
  `TypeRef` forms instead of raw parser text
- member and scoped member path reconstruction render indexed path segments
  from parsed expression shapes instead of splicing raw index text
- generic method/template lookup text now renders through normalized
  `TypeRef` arguments for both parsed and compatibility template argument
  paths
- semantic template-call callee construction also renders bracket arguments
  through normalized `TypeRef` lookup text instead of reading raw compatibility
  expression text
- template-call C++ emission lowers pointer-cast targets and builtin template
  constructors through parsed `TypeRef` nodes instead of rebuilding bracketed
  type strings
- template-call C++ emission now synthesizes compatibility text for temporary
  `TypeRef` nodes from the parsed type shape instead of copying the template
  call expression text span
- pointer type C++ emission helpers and pointer-cast call emission now wrap
  parsed pointee types in explicit `TypeRef::Pointer` nodes instead of
  concatenating `*` onto type text and reparsing it
- emitted-local receiver base type inference reuses the shared parsed
  `TypeRef` head-name helper for named, templated, function, and value types
- inferred generic method instantiation renders inferred `TypeRef` arguments
  through the shared type helper instead of reading raw type text
- local scopes preserve parsed `TypeRef` nodes for declared parameters,
  constants, locals, catch bindings, and typed loop bindings, so function
  pointer calls can check signatures without reparsing declared type text
- indexed-local and iterable-local semantic lookup now enters through the shared
  typed-first local `TypeRef` helper before touching compatibility local type
  strings
- assignment-target and local callback signature semantic lookup also enter
  through the same typed-first local `TypeRef` helper
- local swizzle C++ emission uses the shared typed-first local lookup instead
  of reparsing rendered local type strings before codegen
- expected generic method C++ emission asks the expression type inference helper
  for argument types instead of special-casing local-name `TypeRef` maps
- LSP loop-binding inference uses shared local and iterable `TypeRef` helpers
  instead of reading local type maps directly
- member-path semantic lookup uses the shared typed-first local `TypeRef` helper
  instead of maintaining a private local-map fallback
- index-assignment C++ emission uses the shared typed-first local `TypeRef`
  helper when looking up `[]=` operator methods
- statement C++ expression-type inference can receive symbol context, so
  inferred local assignments, match subjects, and generic method arguments
  resolve local type aliases before parsing compatibility local type strings
- index expression semantic checks use typed local lookup when resolving `[]`
  operator signatures
- indexed-assignment semantic checks use typed local lookup when resolving
  `[]=` operator signatures
- column and channel slice C++ emission read fixed-array shapes from local
  `TypeRef` metadata before falling back to compatibility local type strings
- pointer/member C++ emission checks pointer receivers from local `TypeRef`
  metadata before falling back to compatibility local type strings
- iterable value tests and callers now use the `TypeRef` iterable helper; the
  older string-only iterable local API was removed
- indexed `cpp(...)` escape inference now threads local `TypeRef` metadata
  through the indexed local helper; the older string-only indexed local API was
  removed
- local callable sema now resolves function types through local `TypeRef`
  metadata and alias `TypeRef`s instead of reparsing the compatibility local
  type string
- fixed-array slice emission now reads shape metadata from local `TypeRef`s
  only, and the no-`TypeRef` slice emit overloads were removed
- pointer receiver detection in C++ expression emit now reads local `TypeRef`
  metadata directly instead of reparsing compatibility local type strings
- swizzle expression and assignment emit now require typed local metadata; the
  no-`TypeRef` swizzle emit overloads were removed
- emitted local type inference now resolves named locals from local `TypeRef`
  metadata only instead of reparsing compatibility local type strings
- indexed local string inference now requires local `TypeRef` metadata and no
  longer falls back to reparsing compatibility local type strings
- index assignment hook emission now requires receiver `TypeRef` metadata and
  the no-`TypeRef` hook overloads were removed
- shared local type lookup now treats `local_type_refs` as authoritative and no
  longer falls back to reparsing compatibility local type strings
- assignment/index expression sema now uses local `TypeRef` presence for local
  typed targets instead of the compatibility local string map
- explicit `cpp(...)` expression inference now uses local `TypeRef` metadata for
  local method/index/name type checks instead of the compatibility local string
  map
- iterable and indexed-local semantic helpers now accept local `TypeRef`
  metadata directly instead of carrying the compatibility rendered local type
  map through APIs that no longer read it
- emitted-local type inference now accepts only local `TypeRef` metadata,
  function return `TypeRef`s, and symbol context; the old rendered local type
  map parameter was removed from that inference path
- address-escape semantic checks now classify local storage from `TypeRef`
  metadata instead of reparsing rendered local type strings
- destructuring shadow checks and inferred-assignment local existence checks now
  use local `TypeRef` metadata instead of the compatibility rendered local type
  map
- local existence checks in semantic call/member handling now use
  `local_type_refs`, and member-path type helpers no longer accept the rendered
  local type map
- enum payload match bindings now go through the shared local-binding helper
  instead of writing rendered and structured local maps by hand
- LSP local-context collection now keeps local `TypeRef` metadata as the primary
  result and renders string types only at hover/completion compatibility
  boundaries
- slice/swizzle and pointer-call emission helpers now avoid unused rendered
  local type parameters when the decision comes from local `TypeRef` metadata
- assignment C++ emission now uses local `TypeRef` metadata to decide whether a
  name is an existing local, and first assignments with unknown native escape
  types store an explicit `auto` `TypeRef` instead of relying on rendered local
  strings
- explicit `cpp(...)` pointer-member rewrites for expression and statement
  escapes now read local pointer/list-pointer facts from `TypeRef` metadata
  instead of reparsing rendered local type strings
- statement block C++ emission no longer exposes string-only local-type
  overloads that parse rendered locals back into `TypeRef`; callers must pass
  structured local metadata alongside the compatibility local-name map
- the type parser now recognizes C++ scoped template spellings such as
  `std::vector<std::string>` as structured `Template` `TypeRef` nodes, and
  builtin method inference uses those parsed children instead of owning a
  separate native-template substring splitter
- receiver-template substitution now has a `TypeRef` path, so method and field
  instantiation can substitute `value_type`/`element_type` placeholders without
  rendering substituted types to strings and reparsing them
- inherited method signature instantiation now has a `TypeRef` receiver path,
  so generic base/interface methods substitute receiver arguments without
  reparsing the rendered receiver type
- inherited method lookup now accepts parsed receiver `TypeRef` nodes, and
  override validation passes parsed base-class references through that path
  instead of rendering base types for lookup
- class instance-storage queries now accept parsed `TypeRef` receivers, so
  super/base-class emission can inspect generic base storage without rendering
  base types first
- native base assignability now accepts parsed expected/got `TypeRef` nodes, so
  typed assignment checks can validate derived-to-base pointer/reference
  assignments without rendering both sides first
- receiver unwrapping now accepts parsed `TypeRef` nodes, and inherited field
  lookup plus swizzle lookup use that path when the receiver/base type is
  already structured
- instance method signature lookup now accepts parsed receiver `TypeRef` nodes,
  and inherited method recursion walks parsed base-class references instead of
  rendering base types before lookup
- instance method signature collection now accepts parsed receiver `TypeRef`
  nodes, so overload-list collection walks inherited base-class references
  without rendering base types before recursion
- static method signature lookup now accepts parsed receiver `TypeRef` nodes,
  so inherited static lookup walks parsed base-class references without
  rendering base types before recursion
- generic method inference now accepts parsed receiver `TypeRef` nodes for the
  normal inference path, so inherited generic method lookup walks parsed
  base-class references without rendering base types before recursion
- expected-return generic method inference now accepts parsed receiver
  `TypeRef` nodes, so contextual return inference for inherited generic
  methods walks parsed base-class references without rendering base types
  before recursion
- expected-type generic method C++ emission now keeps receiver inference as
  parsed `TypeRef` metadata and walks inherited base-class references without
  rendering receiver/base types before template argument inference
- inheritance traversal helpers for derives-from checks, abstract-method
  resolution, inherited field/method collection, storage checks, and method
  declaration lookup now walk parsed base-class `TypeRef` nodes instead of
  rendering base types before recursion
- `super` method and `super.init` inference now keep selected base classes as
  parsed `TypeRef` nodes, passing structured receiver types into constructor
  and method checks and rendering only for diagnostics
- array literal shape inference now carries the inferred element type as a
  parsed `TypeRef`, and semantic element checks use that structured metadata
  instead of reaching back through rendered declaration text
- iterable binding inference now exposes only `TypeRef` helpers; the remaining
  string-only iterable element APIs were removed, and LSP local binding
  inference uses the same structured iterable element path as semantic checks
- indexed local inference now exposes only the `TypeRef` helper; `cpp(...)`
  escape inference uses structured indexed-local metadata internally and
  renders only at the escape compatibility boundary
- loop binding compatibility now resolves aliases through `Symbols::alias_type_refs`
  and compares parsed `TypeRef` nodes instead of resolving binding and element
  types through string aliases
- type aliases preserve parsed `TypeRef` nodes in the symbol table, allowing
  local callback aliases such as `type Visit = fn(...)` to resolve through the
  structured type path
- callback alias lookup follows parsed `TypeRef` alias chains with a cycle
  guard before falling back to resolved type text
- type-shaped builtins such as `new[T]`, `malloc[T]`, `sizeof[T]`,
  `alignof[T]`, and `offsetof[T]` validate parsed `TypeRef` arguments
  recursively, so nested unknown types get precise diagnostics
- allocation inference for parsed `new[T]` and `malloc[T]` renders the
  allocation type through the shared `TypeRef` helper instead of reading raw
  type-argument text
- explicit `cpp(...)` allocation inference names its raw-callee boundary
  separately, while parsed `new[T]` and `malloc[T]` calls use parsed `TypeRef`
  arguments
- source range fields on statement, expression, and type nodes
- semantic diagnostics for return values, local initializer values, local type
  names, conditions, and assignment targets use expression/type node locations
  where available
- initial `TypeRef` C++ lowering is implemented and used for type aliases
- enum forward declarations and enum definitions emit underlying types from
  parsed `TypeRef` nodes instead of the compatibility raw type string
- class base declarations preserve parsed `TypeRef` nodes, so base validation,
  duplicate diagnostics, generic base substitution, and C++ inheritance
  emission do not reparse the compatibility base string
- declaration validation walks parsed `TypeRef` nodes recursively for aliases,
  enum underlying types, fields, parameters, returns, constants, and static
  fields, so nested unknown types in containers/callbacks are diagnosed at the
  nested type source location
- declaration type-shape validation, including Dudu tuple arity limits, also
  walks `TypeRef` nodes instead of re-splitting type strings
- function-body validation uses the same recursive `TypeRef` checks for local
  variable annotations, catch bindings, and typed `for` loop bindings
- template calls keep template arguments separate from runtime call arguments,
  and C++ emission lowers them from the parsed expression node
- normal template-call emission lowers bracket arguments from parsed `TypeRef`
  nodes, including non-type value arguments, instead of falling back to raw
  expression text
- template-call lookup names for parsed type arguments and pointer-cast
  inference results render `TypeRef` nodes through the shared type helper
  instead of reading raw type text
- empty parsed template calls such as `identity[]()` are rejected during
  semantic checking instead of falling through raw expression inference and
  emitting invalid C++
- unresolved parsed dotted template calls such as `missing.namespace[i32]()`
  are rejected after native/header lookup misses, while declared native import
  prefixes still return `auto` explicitly
- unsupported parsed template callees such as `(1)[i32]()` are rejected instead
  of being emitted as raw C++
- template calls also keep parsed `TypeRef` nodes for bracketed arguments, so
  type-shaped builtins such as `new[T]`, `malloc[T]`, `sizeof[T]`,
  `alignof[T]`, `offsetof[T]`, and empty `list[T]`/`dict[T]`/`set[T]`
  construction can check and emit type arguments without reparsing them as
  expression text
- `TypeRef` supports compile-time value template arguments such as the `3` in
  `std.array[i32, 3]`, so native C++ templates with integer non-type
  parameters do not masquerade as missing Dudu types
- ordinary calls and template calls keep a parsed callee expression alongside
  the compatibility `name` field, so sema/emission/LSP can migrate away from
  raw callee strings incrementally
- direct callee-name extraction is shared in the AST expression helper layer
  instead of being reimplemented by individual semantic passes
- bare-call and member-call name helpers let semantic checks reject dynamic
  Python builtins and local-address escape calls from parsed callee shape
  instead of reconstructed dotted strings
- slice endpoint checks in expression/index sema and C++ emission use the
  shared parsed expression-presence helper instead of raw child text emptiness
- sema and C++ emission share AST-level expression presence helpers, so
  whitespace-only unknown expressions are handled consistently without
  duplicating raw-text checks
- ordinary call C++ emission lowers parsed callee expressions instead of
  lowering the callee name string directly
- `super.init(...)` recognition in sema and class emission checks parsed
  `super` member-call shape instead of comparing reconstructed callee text
- template-call fallback emission and pointer-cast emission use parsed callee
  helper paths for non-keyword callees
- templated pointer casts such as `*const[i32](ptr)` validate parsed
  `TypeRef` arguments recursively and lower through the AST template-call path
  instead of falling into generic template-call emission
- `offsetof[T](field)` validates the field designator through the parsed field
  expression and lowers bare, dotted, and string field names structurally
  instead of emitting raw child expression text
- ordinary call semantic inference uses a callee lookup string reconstructed
  from the parsed callee expression where possible
- parsed method-call inference handles typed expression receivers such as
  `make_counter().get()`, including Dudu-source diagnostics for unknown
  methods, instead of falling back to raw expression inference
- unresolved parsed dotted calls such as `missing.namespace()` are rejected
  during semantic checking after native/header lookup misses instead of being
  emitted as raw C++
- unrecognized parsed call and template-call expressions now produce Dudu
  diagnostics after structured lookup misses instead of falling through to raw
  expression inference; `cpp(...)` remains the explicit native escape hatch
- imported native namespace calls without scanner metadata, and method calls on
  local `auto` receivers from native APIs, stay on parsed call nodes and return
  `auto` explicitly instead of using raw expression fallback
- final local-method lookup recognizes string-like native C++ types such as
  `std.string_view`/`std::basic_string_view` for common `size`, `length`, and
  `empty` calls instead of relying on raw fallback
- removed Python `lambda` expressions parse only far enough to produce Dudu
  unsupported-feature diagnostics; the sema/codegen callback adapters and raw
  lambda string rewriter have been removed in favor of statement-only named
  `def` declarations
- template-call semantic inference and template method lookup use the parsed
  callee expression when reconstructing lookup names
- templated pointer-cast semantic inference builds the pointee from parsed
  template `TypeRef` arguments instead of reconstructing `Name[...]` text and
  reparsing it
- generated local type inference for call expressions derives callee lookup
  names from parsed callee nodes
- generated local type inference no longer has a raw-string helper for
  `Unknown` expression nodes; unsupported expression shapes are rejected before
  emission instead of guessed from text
- parsed member/callee path reconstruction lives in a shared AST expression
  helper instead of duplicated sema/emission utilities
- AST type compatibility checks use parsed call callees for explicit casts and
  `Ok(...)`/`Err(...)` result construction
- Result compatibility checks for direct `Ok(...)` and `Err(...)` construction
  read the parsed call name instead of reconstructing callee text
- explicit-cast compatibility checks read parsed direct call names before
  falling back to reconstructed callee text for unusual callee shapes
- type compatibility exposes parsed `TypeRef` overloads, and annotated local
  initializers validate against the parsed expected type node when available
- return statement compatibility now carries the function or method return
  `TypeRef` in `FunctionScope` and checks returned expressions against that
  parsed type node while preserving the existing return-mismatch diagnostics
- semantic highlighting uses parsed call callees so method calls can color the
  receiver and called member separately instead of treating dotted callees as
  one raw span
- semantic highlighting merges native header metadata before token collection,
  so direct and aliased C/C++ types, functions, values, and macros can carry
  the `native` modifier without emitting token ranges from header files
- semantic highlighting computes member-token locations from parsed receiver
  expression ranges instead of deriving the member column from raw expression
  text length
- array/list/dict index type inference uses parsed index expressions where
  available, so tuple-shaped multi-index expressions no longer depend on raw
  comma splitting
- indexed and iterable element inference reads parsed `TypeRef` wrappers and
  template children for `list[T]`, `dict[K, V]`, `span[T]`, and storage-like
  wrappers instead of slicing those type strings directly
- index and iterable inference unwrap reference/const receivers through
  parsed `TypeRef` nodes, keeping the text wrapper only as a compatibility
  boundary for callers that still pass rendered types
- indexed and iterable fallback element extraction for references, pointers,
  fixed arrays, and single-argument template containers renders parsed
  `TypeRef` children through the shared type helper instead of reading child
  text directly
- generated local C++ type inference also reads parsed `TypeRef` template
  children for local `list`, `span`, `set`, and `dict` index receivers instead
  of carrying a separate template-argument splitter
- generated local C++ type inference unwraps parsed receiver/pointer child
  types through the shared `TypeRef` helper instead of reading child text
  directly
- generated C++ block emission now preserves parsed local `TypeRef` metadata
  alongside emitted local type strings, so nested statements, match payload
  bindings, local indexing, and expected generic method lowering can use the
  structured local type path
- generated local C++ type inference resolves method-call result types from
  parsed callee receiver expressions, so `make_counter().get()` does not
  depend on splitting a dotted callee string
- generic call-site inference reads parsed `TypeRef` nodes for nested
  templates, fixed arrays, and pointer/reference wrappers instead of carrying
  local bracket-splitting logic
- generic receiver template argument extraction reads parsed `TypeRef` template
  children instead of hand-slicing bracket text
- Dudu-native generic class and function instantiation substitutes type
  parameters through parsed `TypeRef` node reconstruction, covering nested
  templates, fixed arrays, wrappers, and function types instead of replacing
  identifiers inside raw type strings or reparsing rendered member types
- generic argument binding, explicit template lookup text for parsed type
  arguments, and generic method receiver argument extraction render parsed
  `TypeRef` nodes through the shared type helper instead of reading child text
  directly
- a shared AST type helper owns `TypeRef` substitution rendering, so generic
  functions, generic classes, and Dudu-native method/class template
  substitutions use the same structured path
- receiver-derived template placeholder substitution also uses the shared
  `TypeRef` path for Dudu-shaped type metadata, with raw identifier replacement
  isolated to native C++ spellings the Dudu type parser cannot represent
- fixed-array indexing and iteration recover `array[T][shape]` element types
  from parsed `TypeRef::FixedArray` nodes instead of hand-matching nested
  brackets in the type text
- local indexed access, indexed assignment, and typed iteration consult parsed
  local `TypeRef` metadata before falling back to compatibility type strings
- public index and iterable inference now try the shared parsed `TypeRef`
  container path first, leaving the old string path as the native/operator
  boundary instead of duplicating list/dict/array logic
- typed index inference now returns structured `TypeRef` nodes directly for
  pointer, list/span/set, dict, and fixed-array cases instead of rendering
  those common containers to text and parsing them back
- typed array column/channel/row slice inference now constructs
  `span[T]`/`strided_span[T]` `TypeRef` nodes from parsed array element types
  instead of formatting those view types and parsing them again
- typed `new[T]`/`malloc[T]` semantic inference now returns pointer `TypeRef`
  nodes from parsed template arguments before the string compatibility API
  renders them for older callers
- parsed template pointer casts such as `*const[i32](ptr)` now return pointer
  `TypeRef` nodes through direct typed expression inference before the legacy
  string expression inference path
- parsed type-shaped builtins `sizeof[T]`, `alignof[T]`, and `offsetof[T]`
  now return `usize` directly from typed expression inference while validating
  their parsed `TypeRef` arguments and `offsetof` field expression shape
- ordinary constructor calls now return named `TypeRef` nodes through direct
  typed expression inference after checking Dudu constructor arguments,
  avoiding the string-returning call inference fallback for common constructors
- generic class constructors such as `Box[i32](value)` now return templated
  `TypeRef` nodes through direct typed expression inference after generic
  argument validation and constructor checking
- ordinary builtin calls such as `len`, `range`, `min`, `max`, `print`,
  `delete`, and `free` now return `TypeRef` nodes through direct typed
  expression inference while preserving arity and argument checks
- enum variant constructor calls such as `Expr.Number(value)` now return the
  enum `TypeRef` directly from typed expression inference after payload checks
- unary expression typing for `not`, numeric negation, bitwise-not,
  dereference, and address-of now returns `TypeRef` nodes directly instead of
  rendering operand types and reparsing pointer/dereference results
- member expression typing now enters through a direct `TypeRef` path for enum
  variants, native member values, Dudu fields, swizzles, and foreign-auto
  member lookups before falling back to legacy string expression inference
- binary expression typing now infers child `TypeRef` nodes first and renders
  them only for compatibility checks, so logical/comparison/contextual numeric
  results no longer start by asking the string expression inferencer
- function and enum argument checking now infers actual argument `TypeRef` nodes
  first and renders them only at the compatibility/error boundary, removing
  another legacy string expression inference pass from ordinary call validation
- legacy string expression inference now delegates unary and binary expressions
  to the structured `TypeRef` helpers instead of duplicating operator semantics
  with a separate recursive string path
- legacy string expression inference now delegates member expressions to the
  structured member `TypeRef` helper instead of duplicating enum, native,
  field, swizzle, and foreign-member checks
- C++ escape compatibility inference now uses structured expression typing for
  validation-only argument walks and parsed unary/binary fallback expressions,
  leaving rendered strings only where the escape boundary returns display text
- call/template inference now uses `check_expr_ast` for validation-only argument
  walks, and builtin `min`/`max` compares parsed actual `TypeRef` nodes instead
  of recursive string expression results
- named-argument, slice, and index expression typing now have direct `TypeRef`
  cases, and the legacy string path delegates to them instead of recursing
  through raw receiver/index type strings
- member-call receiver typing and C++ escape `Ok`/`Err` rendering now use
  structured `TypeRef` inference, leaving `infer_expr_ast` as the central legacy
  string-rendering fallback rather than a dependency of normal call inference
- expression `TypeRef` inference now has explicit cases for all expression
  kinds; the old generic fallback into `infer_expr_ast` is gone, with remaining
  string compatibility isolated to call/template-call paths that still need it
- the dead `infer_expr_ast` string expression inferencer has been removed; expression
  sema now exposes structured `infer_expr_type_ast` plus explicit compatibility
  renderers for C++ escapes, calls, and template calls
- `member_path_type_from_string` has been removed from sema internals; explicit
  `cpp(...)` escape inference now owns the remaining string-to-expression parse
  it needs for raw member path compatibility
- statement C++ emission no longer has local string wrappers for typed
  assignment lowering or template-kind checks; missing local metadata is parsed
  once at the compatibility boundary and the normal path uses `TypeRef`
- emitted local type inference now extracts receiver base names directly from
  `TypeRef` nodes and no longer reparses rendered receiver type strings for
  method-return lookup
- LSP local variable/parameter type collection for hover, member completion,
  and member definition now walks parsed function/method bodies and reuses
  semantic expression inference instead of regexing source lines
- same-document LSP reference and rename locations now come from parsed
  declarations, statements, and expression nodes instead of raw token scans, so
  comments and string literals are no longer treated as editable references
- LSP reference and rename requests now require the cursor to resolve to an AST
  symbol before scanning the workspace, so strings/comments cannot seed a rename
  or reference query
- C/C++ header go-to-definition now uses parsed foreign import ranges from the
  AST instead of regexing the active source line
- LSP go-to-definition now resolves its symbol/path from the AST before looking
  up Dudu, imported, native, or member definitions, so comments and strings do
  not act like definitions
- LSP hover now uses the same AST cursor path lookup before resolving Dudu,
  imported, native, member, or local hover text
- array shape inference and explicit shape extraction also consume parsed
  `TypeRef` array forms, removing the separate bracket matcher from
  `array_shape.cpp`
- local declaration array shape inference and explicit-shape checks now use
  the parsed statement `TypeRef` directly instead of reparsing the annotated
  type string; explicit fixed-array literal checks infer the initializer shape
  from the parsed `array[T]` storage child rather than synthesizing and
  reparsing an `array[...]` type string
- exception binding locals construct their `&const[T]` `TypeRef` directly
  from the parsed catch type instead of reparsing a synthesized type string
- local declaration C++ emission checks `Option`, fixed arrays, and
  list/dict/set literal initialization against parsed `TypeRef` nodes when the
  type came from source syntax, leaving parsing only for inferred synthetic
  array types
- raw-string C++ type lowering delegates Dudu fixed-array forms such as
  `array[T][N]` to parsed `TypeRef::FixedArray`, leaving only C-style
  `T[N]` fallback parsing in the string path
- member field inference for `Result[T, E]` reads parsed `TypeRef` template
  children for `.value` and `.err` instead of re-splitting the result type text
- shared AST type helpers own common parsed template-child and unary-wrapper
  extraction, so semantic modules do not each carry local bracket-parsing
  versions of the same TypeRef logic
- core semantic helpers such as `base_type` and tuple member extraction read
  parsed `TypeRef` node shapes instead of stripping pointer/reference prefixes
  and tuple brackets by hand
- method, inheritance, and operator receiver/type unwrapping use parsed pointer,
  reference, and wrapper `TypeRef` nodes instead of local string-prefix loops
- builtin method signatures for Dudu-native `list[T]` and `atomic[T]` receivers
  use parsed `TypeRef` helpers for receiver stripping and element/value types
- builtin method signatures for Dudu-shaped `list[T]`, `set[T]`, `dict[K, V]`,
  `Option[T]`, and `atomic[T]` receivers now read parsed `TypeRef` children
  first, with native angle-bracket spelling kept only as imported C++ fallback
- native C/C++ overload checks for ordinary and explicit-template parsed calls
  consume expression children directly instead of flattening arguments back to
  strings
- constructor and native overload validation expose only AST argument APIs; the
  old string-vector overloads and string matching helpers have been removed
- legacy string expression inference parses call, method, operator, and index
  arguments into `Expr` nodes before using shared semantic checks; the old
  string call-argument checker has been removed
- parsed `new[T]` and `malloc[T]` allocation calls validate argument counts
  from expression children instead of stringified call arguments
- allocation semantic helpers expose AST argument APIs for call-shaped
  allocation checks; the old string-vector overload has been removed
- delete/free semantic checks classify pointer arguments through parsed
  `TypeRef` nodes instead of checking raw leading `*` spelling
- legacy string expression inference now parses pointer-cast, allocation,
  deallocation, and `Ok`/`Err` call arguments into expression nodes before
  checking them
- legacy string expression inference delegates bool/string literal,
  `not`/logical, and comparison expression shapes to parsed expression sema;
  the old string-splitting `sema_expr` helpers have been removed
- legacy string expression inference delegates binary operator shapes to parsed
  expression sema; the old top-level operator splitter fallback has been
  removed
- operator compatibility checks now expose only parsed-expression overloads;
  the old raw right-hand-expression overloads have been removed
- type compatibility now separates type-only assignment checks from
  expression-aware assignment checks; raw expression-string assignment APIs and
  their duplicate literal/container parsing helpers have been removed
- AST-backed statement checks route assignment compatibility through expression
  nodes instead of using raw statement value strings
- compound-assignment target checks use the parsed target expression path
  instead of feeding the target text back into legacy target analysis
- assignment target checks use parsed target expressions only, including
  call/template-call lvalue targets such as native reference-returning accessors;
  the legacy raw target-string fallback has been removed
- dereference assignment targets such as `*ptr = value` use typed expression
  inference and peel pointer `TypeRef` nodes instead of reparsing rendered
  pointee type strings
- match subject checks now pass inferred `TypeRef` nodes into wrapper-match
  detection, and match guard checks use typed expression inference before
  rendering only for diagnostics
- condition checks for `if`, `while`, and assert-like statements use typed
  expression inference before rendering only for diagnostics and bool-operator
  lookup
- loop binding inference for non-name iterables uses inferred `TypeRef`
  iterable metadata directly instead of rendering and reparsing the iterable
  type string
- compiler tuple destructuring now reads RHS tuple child `TypeRef` nodes and
  binds destructured locals with structured type metadata; alias/native spelling
  fallback remains at the compatibility boundary
- LSP local-context tuple destructuring mirrors the structured compiler path,
  and the old public `tuple_types` string helper has been removed
- tuple child extraction with alias fallback now lives in a shared `TypeRef`
  helper, so compiler sema and LSP do not carry duplicate rendered-string tuple
  parsing logic
- LSP local-context implicit bindings now use typed expression inference for
  untyped local declarations and first assignments, and the local string
  expression inference helper has been removed
- LSP local-context `for` bindings now infer element types from stored
  iterable `TypeRef` metadata or typed iterable expression inference, and the
  local text-to-`TypeRef` adapter has been removed
- TypeRef-backed assignment checks now infer RHS expressions as `TypeRef`
  first, use structured type assignment before compatibility fallback, and
  render only for legacy assignment/literal checks and diagnostics
- generic function/method inference now requires typed expression callbacks and
  no longer falls back to inferring rendered argument type strings and parsing
  them back into `TypeRef`
- body semantic checking now requires a typed expression callback, so statement
  checks cannot silently run without structured expression type inference
- array literal element checks now carry explicit and inferred element types as
  `TypeRef` nodes and try structured assignment before legacy literal
  compatibility fallback
- text-expected body type checks now infer RHS and generic-method receivers as
  `TypeRef` first, using structured type assignment before legacy
  assignment/literal compatibility fallback
- type compatibility has AST overloads for simple literals and list/set/dict
  literals, including expected-type disambiguation for empty `{}` dict
  initializers, reducing reliance on string parsing for assignment checks
- assignment compatibility for `list[T]`, `set[T]`, `dict[K, V]`,
  `Option[T]`, and `Result[T, E]` now reads parsed `TypeRef` template children
  instead of open-coding bracket slicing for those common type shapes
- `TypeRef` assignment compatibility for annotated local initializers keeps
  `list`, `set`, `dict`, `Option`, and `Result` expected types on parsed
  template-child nodes instead of rendering them back to strings first
- the string-facing assignment compatibility entry point now routes container,
  `Option`, `Result`, and `variant` literal checks through the parsed
  `TypeRef` helper, removing the duplicate manual template-text path for those
  forms
- annotated assignment compatibility now parses inferred Dudu-shaped `got`
  types back into `TypeRef` nodes and tries structural compatibility before
  falling back to native spelling compatibility
- string-facing type and assignment compatibility now try parsed structural
  `TypeRef` matching before spelling fallback, so callers that still pass type
  strings get the AST path for Dudu-shaped pointer, reference, wrapper,
  template, fixed-array, and function types
- pointer/reference assignment compatibility uses parsed `TypeRef` pointer,
  reference, and const-wrapper nodes instead of testing `*`, `&`, and
  `*const[...]` spelling by hand
- `TypeRef`-backed pointer/reference compatibility now also handles void
  pointer targets, const pointer binding, pointer-to-reference values, value
  from reference/const wrappers, and native function-pointer compatibility
  before falling back to string-facing native spelling checks
- the `TypeRef` to `TypeRef` compatibility path now performs structural
  matching for pointers, references, const/transparent wrappers, templates,
  fixed arrays, and function types before falling back to native spelling
  compatibility
- function type normalization for compatibility checks parses `TypeRef`
  function nodes and renders through the shared AST type helper, including
  omitted-return `fn(...)` signatures
- C++ tuple-element artifact normalization now reads parsed `TypeRef` template
  children for `__tuple_element_t[N, tuple[...]]` and qualified
  `std.tuple[...]` forms instead of manually splitting nested bracket text
- native C++ type-artifact normalization now exposes a parsed `TypeRef` entry
  point; string-facing callers parse once at the compatibility boundary, while
  tuple-element and non-array template cleanup operate on structured type
  nodes
- AST literal classification no longer reclassifies `Unknown` expression nodes
  from raw text
- C++ assignment emission detects `Option` reset from `NoneLiteral` expression
  nodes instead of raw value text
- C++ expression emission lowers simple names and member paths structurally;
  the broad raw expression rewriter is only used at the explicit expression
  `cpp(...)` escape boundary in statement emission
- generated local C++ type inference uses parsed expression nodes and no longer
  has a raw-string helper for `Unknown` expression shapes
- generated local method-return inference unwraps pointer/reference/storage
  receiver types through parsed `TypeRef` nodes instead of string-prefix loops
- generated local C++ type inference also follows parsed index expressions for
  local `list`, `dict`, `set`, `span`, and shaped `array` receivers, so common
  `value = items[i]` assignments do not fall back to raw expression text
- generated local C++ type inference follows parsed index expression receivers,
  so `value = make_values()[0]` and chained fixed-array row indexes can infer
  types without raw expression parsing
- generated local C++ type inference handles parsed literals, unary
  address/deref/not/minus/bitwise-not expressions, binary expressions,
  conditionals, and parsed template callees conservatively, so common
  `value = count + 1` style locals do not need raw expression inference
- C++ `if constexpr` detection for build-only conditions uses parsed condition
  expressions and no longer has an unknown-expression raw text fallback
- C++ expression emission writes string, integer, and float literal expression
  nodes directly, and lowers `str(value)` calls through an explicit AST path
  instead of sending them through raw expression lowering
- C++ expression emission lowers parsed plain-name nodes directly through
  namespace alias qualification instead of sending them through full raw
  expression rewriting
- nested fixed-array literal scalar emission uses parsed expression nodes, so
  Dudu numeric literal normalization and other scalar expression lowering stays
  consistent inside array initializers
- semantic checks for assignment through `*ptr`, plain names, members, indexes,
  and native reference-returning calls use parsed target expressions instead of
  raw target text
- member-path type checks reconstruct paths from parsed name/member/index
  expression nodes instead of directly trusting the original expression text
- parsed member expression inference handles typed expression receivers such as
  `make_point().x`, including Dudu-source diagnostics for unknown fields,
  instead of falling back to raw expression inference
- semantic checks and C++ statement emission use expression-node presence for
  optional statement values/messages instead of raw statement value strings
- `for` loop semantic checks and C++ emission use parsed iterable expression
  presence instead of the raw iterable statement string
- typed `for` loop iterable binding checks consume the parsed iterable
  expression rather than the raw iterable statement string
- typed `for` loop binding compatibility now uses the parsed binding
  `TypeRef`, with rendered strings kept only as the alias/native spelling
  fallback
- typed `for` loop binding fallback and diagnostics render from the parsed
  binding `TypeRef` instead of reading the compatibility type text field
- enum match wildcard and guard checks use parsed case pattern/guard
  expressions instead of raw case statement strings
- wrapper match subject classification for `Option[T]` and `Result[T, E]`
  reads parsed `TypeRef` template children instead of manually slicing wrapper
  type strings
- `except` binding validation parses malformed bare headers into
  `condition_expr` and uses parsed binding/type fields instead of the raw
  except-header condition string
- semantic compatibility checks for parsed binary and comparison operators use
  the right-hand expression node instead of stringifying it for assignment rules
- native overload matching and constructor semantic checks use parsed argument
  expression nodes for assignment compatibility instead of passing argument text
  back through string-based compatibility callbacks
- constructor semantic checks now preserve parsed field and `init` parameter
  `TypeRef` nodes and infer argument expressions as `TypeRef` before falling
  back to rendered compatibility checks and diagnostics; constructor and
  `super.init` checking no longer accept a string expression inference callback
- native overload matching now receives typed expression inference callbacks,
  checks parsed argument and parameter `TypeRef` nodes before rendered
  compatibility fallback, and exposes a parsed `TypeRef` path for native
  template placeholder binding; native signature matching no longer accepts a
  string expression inference callback
- explicit native template placeholder discovery now walks parsed return and
  parameter `TypeRef` nodes before falling back to native spelling scans
- explicit native template substitution now rewrites parsed parameter and
  return `TypeRef` nodes first, then renders compatibility strings from those
  nodes; raw text replacement remains only for signature pieces without parsed
  type metadata
- Dudu-declared function return types and out-of-line receiver types are now
  stored as parsed `TypeRef` nodes instead of duplicate raw declaration
  strings; rendered strings are derived at sema/codegen/display boundaries
- native overload matching also checks numeric promotion through parsed
  parameter and argument `TypeRef` nodes before falling back to rendered native
  spelling
- generic function and method inference no longer accepts a string expression
  inference callback; inferred inputs cross that API as `TypeRef` nodes
- body and match semantic callback APIs now expose typed expression inference
  only, removing the old string expression inference callback from those phase
  boundaries
- assignment target validation and side-effect-only statement checks now infer
  expression `TypeRef` nodes for swizzle receivers, call targets, `range`
  arguments, assert/raise payloads, expression statements, and void-return
  value checks
- diagnostics and generated default assert messages now use an AST expression
  display helper for structured nodes instead of reading raw expression text
  directly; member/index expression diagnostics and indexed-assignment labels
  use the same structured display path
- member-path semantic diagnostics for parsed member/index expressions now use
  the AST display helper for labels; the remaining string member-path API is a
  compatibility boundary that still needs replacement with structured path
  nodes
- the string member-path type resolver is explicitly named
  `member_path_type_from_string`, and current callers are confined to the
  C++ escape inference boundary rather than normal Dudu member sema
- `member_path_type_from_string` now parses the compatibility string once and
  delegates to structured `Member`/`Index` expression typing instead of walking
  dotted paths with its own string parser
- the string member-path helpers are declared only through the expression
  internal boundary used by explicit C++ escape inference; the public sema
  methods surface exposes the parsed `member_expr_type` path for normal callers
- Dudu-native constant aliases created by selective imports now build a parsed
  `Name` expression directly instead of reparsing the imported identifier from
  text inside the module loader
- semantic-token native lookups for member/callee expressions now derive the
  dotted path from parsed member nodes instead of reading the compatibility
  expression text field first, and no longer fall back to compatibility text
  when a parsed member path is unavailable
- LSP symbol-at and reference collection locate member names from parsed
  receiver expression ranges instead of shifting by the compatibility
  expression text width
- assignment compatibility for parsed explicit casts, value-wrapper
  assignments, and `Ok(...)`/`Err(...)` result construction inspects call
  expression nodes instead of rediscovering those forms from raw text
- removed Python `lambda` expressions keep enough parsed shape for diagnostics
  but no longer participate in normal type inference or emission
- named-argument constructor emission relies on parsed `NamedArg` nodes only;
  the old raw `field=value` child-text fallback has been removed
- parsed call emission lowers Dudu `list.append(...)` calls to C++
  `push_back(...)` at the callee node instead of relying on raw expression
  replacement
- C++ member expression emission lowers proven pointer receivers from parsed
  receiver nodes (`item->field`, `items[i]->field`) without falling through a
  normal-expression raw member rewrite path
- the remaining pointer-member text rewriter is confined to the explicit
  `cpp(...)` escape boundary and classifies pointer locals and lists of pointer
  elements from parsed `TypeRef` nodes instead of string prefixes
- C++ member expression emission lowers normal value receivers from parsed
  receiver nodes and applies namespace alias qualification directly, avoiding
  the full raw expression rewrite path for `player.field` and `std.sin` shapes
- C++ call emission lowers parsed method calls on pointer-typed member
  receivers such as `self.left.backward(...)` as `self.left->backward(...)`,
  using the existing member-path type information instead of raw pointer-member
  rewriting
- C++ call emission now decides pointer-typed expression receivers through the
  parsed member expression type walker rather than reconstructing a member path
  string first.
- C++ expression emission now preserves symbol context while lowering nested
  callee, member, dict-entry, named-argument, index, collection literal,
  tuple, template-call argument, swizzle, pointer-cast, and fixed-array literal
  child expressions instead of dropping to symbol-less child lowering.
- C++ pointer-cast emission lowers cast target types through parsed `TypeRef`
  nodes for both normal and templated call shapes instead of prefixing raw type
  strings and sending them through the legacy type lowerer
- integer and float literal parsing recognizes underscore separators, and
  integer literals recognize `0x`, `0b`, and `0o` prefixes so systems-style
  constants stay on the literal AST path
- LSP local type inference recognizes the same systems numeric literal shapes,
  keeping hover behavior aligned with compiler literal parsing
- default `assert` message emission uses the parsed condition expression text
  instead of the raw statement condition field
- frontend AST shape tests cover systems integer literals and `Slice`
  expression nodes directly
- nested expression ranges advance through whitespace and delimiters for call,
  template-call, list, dict, set, tuple, index, and slice shapes instead of
  blindly inheriting parent expression starts
- unary, binary, and conditional expression children keep source locations on
  the child expression text rather than on the outer expression
- local address escape checks walk parsed return/call/unary expression nodes
  for `return &local` and container `.append(&local)` / `.push_back(&local)`
  cases instead of scanning raw statement text
- parsed index expression type inference uses the AST receiver and index nodes
  regardless of whether the caller supplied an explicit diagnostic location
- indexed type inference no longer exposes a public string-index overload;
  callers must provide parsed index expressions. Explicit `cpp(...)` escape
  type inference parses indexed member-path indexes into `Expr` nodes before
  asking sema for the indexed type.
- parsed index expression type inference handles expression receivers such as
  `make_values()[0]` and chained array rows without falling back to raw
  expression inference
- fixed-array row, column, channel, and trailing full-slice inference now reads
  shape and element metadata from parsed `TypeRef` nodes before native/operator
  fallback, keeping the public index typing path off raw bracket parsing
- fixed-array index-count inference and emitted local index type inference also
  use parsed `TypeRef` shape/element metadata instead of calling the
  string-facing array-shape helpers
- string-facing index type inference now parses the resolved input type once
  and reuses that `TypeRef` for shape/slice/index checks before crossing the
  remaining native/operator spelling fallback
- member-path type checks for indexed local receivers such as `items[i].field`
  parse the indexed segment into an `Expr::Index` and route the index through
  parsed expression typing instead of slicing raw index text
- member-path type checks for normal local value paths now walk parsed
  `Name`/`Member`/`Index` expression nodes directly. The string path remains
  for compatibility boundaries such as explicit C++ escapes and native import
  spelling.
- C++ member expression emission uses parsed receiver type information for
  static-object-then-instance-field access such as `Palette.WHITE.y`, avoiding
  capitalization guesses that would emit `Palette::WHITE::y`.
- assignment target checks for nested indexed member paths such as
  `items[0].pos.x = value` now use the same parsed expression member-path
  walker before falling back to compatibility spelling paths.
- expression and assignment sema for `class.name` static access now carry the
  current class through parsed member expressions instead of normalizing a
  reconstructed member-path string.
- ordinary and templated call sema resolve `class.method(...)` from the parsed
  callee expression, so the old current-class string normalizer has been
  removed.
- current-class-aware member/callee path reconstruction now lives in shared
  sema helpers and is used by body-level generic method call argument checks,
  not only expression sema.
- generic method calls on nested receivers such as `slots[0].box.id[i32](x)`
  type-check through the parsed callee receiver expression before falling back
  to compatibility spelling paths.
- enum variant recognition for expression sema, codegen, and match patterns
  uses a shared structural `Name.Member` helper instead of duplicate dotted
  string reconstruction.
- native import member type lookup walks parsed member expression nodes in
  `sema_native` and converts to native table spelling only at the native
  metadata boundary.
- dereference assignment targets now type-check through the parsed operand
  expression, so normal Dudu code can assign through pointer fields such as
  `*self.out += 1` without using `cpp(...)`
- prefix dereference parsing now binds over postfix member/index/call shapes
  such as `*self.out`, while type-shaped calls such as `*struct State(ptr)`,
  `*i32(ptr)`, and `*list[T](ptr)` stay on the pointer-cast AST path
- malformed parsed index expressions such as `values[]` are rejected during
  semantic checking instead of being emitted through raw expression fallback
- build flag validation walks parsed expression nodes for constants,
  `static_assert`, and normal statements; raw text scanning remains only for
  explicit C++ escape statements
- parser statement pieces now retain their originating token spans and feed
  expression/type spans directly into token-backed parsers, avoiding the old
  stringify-then-relex path for returns, conditions, loop iterables, case
  patterns and guards, exception bindings, declarations, assignments, and
  expression statements
- declaration parser pieces now use those same token-span helpers for class
  bases, fields, enum underlying types and payloads, aliases, function returns,
  parameters, constants, and static assertions instead of reparsing joined
  declaration strings
- assertion statement parsing splits the optional message with token-depth
  awareness instead of string comma scanning, so commas inside calls or
  collection literals stay inside the condition expression
- unsupported Python call checks for `eval`, `exec`, `getattr`, and `setattr`
  walk parsed call expressions instead of scanning raw unknown-expression text
- unsupported `await` and expression-level `yield` forms parse into AST nodes
  and are rejected by both the unsupported-feature pass and expression sema
  without relying on raw text scanning
- unsupported Python-only statement forms such as `with`, `finally`, local
  `def`, local imports, and rebinding declarations classify as structured
  unsupported statement nodes with feature labels instead of masquerading as
  expression statements and being rediscovered by a prefix scan
- malformed lambda expressions are rejected during semantic checking instead of
  being emitted through the raw expression fallback and left to the C++ compiler
- standalone slice expressions are rejected during semantic checking, and parsed
  expression nodes that fail structural C++ lowering no longer fall back to raw
  expression rewriting
- malformed unary, binary, and conditional expression nodes are rejected during
  semantic checking with Dudu diagnostics instead of falling through legacy
  expression inference
- AST parser implementation is split by responsibility: common parse utilities,
  top-level scanners, token-backed type parsing, token-backed expression
  parsing, statement parsing, and public AST helpers now live in separate files
  instead of one oversized mixed parser file

Still too string-based:

- expression-level explicit `cpp(...)` escape hatches still use the old C++
  expression rewriter/inference boundary
- raw macro shapes
- user-facing macro/decorator forms still need deeper AST nodes
- old lambda parameter checking and typed non-capturing C++ lambda
  emission have been removed. Callback examples now use named function
  declarations and pass function names as values.
- some type compatibility and native-header checks still route through type
  strings after `TypeRef` parsing
- exact original-token ranges inside function bodies; current body-node ranges
  now use the first and last real statement tokens, and statement nodes preserve
  reconstructed source spacing for child expression/type location lookup

## Target Architecture

Keep the outer `ModuleAst`, and make function bodies real statements and
expressions all the way through checking and lowering.

Recommended high-level compiler pipeline:

```text
source
  -> tokens
  -> parse tree / AST
  -> name resolution
  -> type checking
  -> lowering checks
  -> C++ AST-ish emit model
  -> generated C++
```

The parser should produce syntax that preserves source locations for every
meaningful node. The type checker should attach resolved types and symbol
links. The emitter should consume typed nodes, not source text.

## Statement AST

Required statement nodes:

- `BlockStmt`
- `ExprStmt`
- `VarDeclStmt`
- `AssignStmt`
- `CompoundAssignStmt`
- `ReturnStmt`
- `IfStmt`
- `MatchStmt`
- `CaseStmt`
- `WhileStmt`
- `ForStmt`
- `BreakStmt`
- `ContinueStmt`
- `TryStmt`
- `ExceptClause`
- `RaiseStmt`
- `AssertStmt`
- `DebugAssertStmt`
- `StaticAssertDecl`
- `CppEscapeStmt`
- `MacroCallStmt`
- `TupleDestructureStmt`

Statements should keep exact source ranges:

- full statement range
- keyword range
- declared name range
- type annotation range
- value expression range

This enables error squiggles on the right token instead of whole-line errors.

## Expression AST

Required expression nodes:

- `NameExpr`
- `LiteralExpr`
- `StringExpr`
- `UnaryExpr`
- `BinaryExpr`
- `CallExpr`
- `TemplateCallExpr`
- `MemberExpr`
- `IndexExpr`
- `SliceExpr`
- `ConstructorExpr`
- `ListLiteralExpr`
- `DictLiteralExpr`
- `SetLiteralExpr`
- `TupleLiteralExpr`
- `LambdaExpr`
- `ConditionalExpr`
- `CastExpr`
- `SizeofExpr`
- `AlignofExpr`
- `OffsetofExpr`
- `NewExpr`
- `MallocExpr`
- `CppEscapeExpr`
- `MacroCallExpr`

Expressions should carry:

- source range
- resolved type after checking
- constant-value info when known
- resolved symbol when applicable
- value category where it matters for C++ interop

## Type AST

Type strings should also become structured.

Required type nodes:

- `NamedType`
- `QualifiedType`
- `TemplateType`
- `PointerType`
- `ReferenceType`
- `ConstType`
- `VolatileType`
- `AtomicType`
- `DeviceType`
- `StorageType`
- `SharedType`
- `FixedArrayType`
- `FunctionType`
- `TupleType`

This matters for generics, native header interop, semantic highlighting, and
diagnostics. A type like `std.unordered_map[str, list[Player]]` should not be
split with ad hoc string code.

## Diagnostics

AST-backed diagnostics should include:

- source span
- primary message
- optional note spans
- optional fix-it edits
- error code
- diagnostic source such as `dudu/parser`, `dudu/sema`, or `dudu/native`

The target quality is Rust/Java-style practical help:

```text
src/main.dd:12:18: error[dudu/type-mismatch]: expected i32, got str
    total: i32 = name
                 ^^^^
note: `name` was declared here as str
    name: str = "bob"
    ^^^^
help: convert explicitly if this is intentional
    total: i32 = i32(name)
```

Recommended fix-it categories:

- add missing import
- qualify ambiguous name
- insert explicit cast
- replace `.` with pointer member access when the type is a raw pointer, if Dudu
  keeps exposing that distinction
- add missing template argument
- add missing return value
- remove unreachable statement
- correct obvious typo from nearby symbol names
- add missing `&` or `*` in type positions when native call signatures require it

## Semantic Highlighting

TextMate highlighting is not enough. The AST and symbol table should feed LSP
semantic tokens.

Token classes:

- namespace/module
- class/type
- type parameter
- function
- method
- constructor
- parameter
- local variable
- field/member
- static field
- constant
- enum
- enum member
- macro
- decorator
- keyword
- operator
- builtin type
- imported native symbol

Useful modifiers:

- declaration
- definition
- readonly
- static
- deprecated
- unsafe/native
- unresolved
- template

This is how Dudu gets real coloring for args, variables, members, classes, and
native symbols.

Status: initial full-document LSP semantic tokens are implemented from the
parsed AST for Dudu declarations, parameters, fields, locals, types, literals,
calls, and member expressions. Native header metadata is merged as a symbol
classification layer, so native types, functions, values, macros, namespaces,
and enum members can carry the semantic-token `native` modifier while token
ranges remain anchored to the open Dudu document.

## LSP Implications

The AST should power:

- diagnostics while editing
- hover with resolved types
- go to definition for Dudu and native symbols
- find references
- rename
- completion
- signature help
- semantic tokens
- document symbols
- workspace symbols
- code actions
- format-aware range edits

The LSP should not reparse everything with shell commands. It should call the
same parser, resolver, and checker APIs used by `duc check`.

## Generics Implications

Native Dudu generics need a typed AST.

The AST must represent:

- type parameters on classes and functions
- template argument lists
- uses of type parameters in fields, params, locals, and return types
- generic method calls
- instantiated function/class signatures
- errors pointing at the failing generic body and the call site that caused the
  instantiation

Without this, generic diagnostics become generated-C++ noise.

## Macro Implications

Macros should operate on syntax nodes or checked declaration metadata, not loose
strings.

The AST should let macros inspect or generate:

- class declarations
- field lists
- function declarations
- enum declarations
- attributes/decorators
- expression nodes for simple expression macros

Raw token macros can remain a C/C++ interop escape hatch. Dudu-native macros
should prefer structured input and structured output.

## Migration Plan

1. Add AST node types alongside the old raw statement bridge: done.
2. Parse simple statements into AST while preserving old lowering for unsupported
   forms.
3. Parse expressions into AST with source ranges.
4. Lower typed AST statements to C++.
5. Move semantic checks from string helpers into AST visitors.
6. Convert formatter and LSP to use AST nodes.
7. Remove raw statement lowering for normal Dudu syntax.
8. Keep `cpp(...)` as the explicit raw C++ escape hatch.

Status: functions and methods now store parsed `Stmt` bodies directly. The old
duplicate `FunctionDecl::body` raw statement storage and raw-body C++/control
flow helper overloads have been removed. The local escape checker also consumes
parsed `Stmt` nodes directly. The parser now constructs `Stmt` nodes directly
instead of staging through a raw statement tree.

Statement semantic checks now enter expression inference through parsed `Expr`
nodes for returns, assignments, local initializer inference, conditions, array
literal element checks, and expression statements. Core AST expression kinds
such as literals, names, unary/binary expressions, tuples, conditionals,
member/index access, collection literals, ordinary calls, and template calls
are handled structurally or rejected with Dudu diagnostics. Native import
prefixes may still return `auto` when scanner metadata is incomplete, and
`cpp(...)` remains the explicit raw C++ escape boundary. Binary expression
parsing covers logical, comparison, bitwise, shift, arithmetic, and modulo
operators.

Comma-separated expression children now keep their own source columns for call
arguments, list literals, tuple literals, set/dict entries, and template-call
arguments parsed through expression lists.

Dudu-defined function calls, function-pointer calls, static class functions,
Dudu-visible method calls, native calls, constructor calls, and template calls
now type-check parsed argument `Expr` nodes directly.

C++ statement emission now lowers return values, assert/debug_assert
conditions, raise values, if/elif/while conditions, for iterables, and bare
expression statements from parsed `Expr` nodes where those nodes are
structurally reliable.

Assert and debug_assert statements now store separate condition and message
expression nodes. Semantic checks and C++ emission no longer split their
condition/message pieces from raw statement strings.

Except clauses now store structured catch binding and catch type fields.
Semantic checks and C++ emission no longer parse catch headers from raw
statement text.

Simple assignment handling now recognizes fresh local bindings and tuple
destructuring from parsed target expressions. C++ emission uses parsed
assignment value expressions for those paths instead of lowering raw right-hand
side text.

Local variable C++ emission now lowers explicit declaration types through the
parsed `TypeRef`. It still uses computed canonical type text when array literal
shape inference changes the declared type.

Top-level function signatures, generated C headers, class method signatures,
class constants, and static fields now lower declared C++ types through parsed
`TypeRef` nodes.

Condition semantic checks now consume parsed statement condition expressions
directly instead of accepting raw condition text and reparsing on mismatch.

`delete` is now a structured statement node with a parsed operand expression.
Semantic checks, C++ emission, and semantic token collection no longer detect
it by scanning raw statement text.

Compound assignment C++ emission now uses parsed target and value expressions
plus the parsed compound operator instead of normalizing raw statement text.

The main statement semantic dispatcher no longer depends on raw statement text
for break/continue diagnostics.

Structured control-flow C++ emission no longer creates a raw statement text
copy before dispatching.

Return statements and common local list, set, and tuple initializers now use
parsed value expression kinds and children for C++ emission instead of raw
comma splitting and substring extraction.

Typed `for` loop binding C++ emission now lowers the binding type through the
parsed `TypeRef`.

Dict literals now parse key/value pairs as `DictEntry` expression nodes.

Template calls now preserve template arguments separately from runtime call
arguments in `Expr::template_args`. C++ expression emission handles ordinary C++
template calls and built-in template-shaped forms such as `new[T](...)`,
`malloc[T](...)`, `sizeof[T]()`, `alignof[T]()`, `offsetof[T](field)`, and empty
`list[T]()`/`dict[K, V]()`/`set[T]()` value construction without falling back to
the raw expression text.

Assignment C++ emission now handles parsed non-binding targets such as member
paths and index expressions directly from `target_expr` and `value_expr`. Bare
name assignment still owns the local-binding inference path.

Local declaration initializer fallback now lowers from the parsed
`value_expr`. Slice index expressions such as `values[1:4]` parse the `1:4`
range as a `Slice` expression node and emit from the parsed index node,
preserving `span[T]` view lowering without requiring a raw whole-expression
rewrite.

`break` and `continue` statements now emit directly from their `StmtKind`
instead of falling through the unknown-statement raw text path.

Member expression typing now has a parsed `TypeRef` wrapper, and typed
expression/index inference plus assignment target checks use it instead of
parsing member type text at each call site.

Assignment C++ emission now uses that parsed `TypeRef` member target path when
coercing non-name assignment values, avoiding another render-type-then-reparse
round trip in normal statement codegen.

Local C++ emission type inference now routes call, template-call, and binary
expression results through the parsed `TypeRef` path. The old public
string-returning helper has been removed; callers that need display text render
from the inferred `TypeRef` at their own boundary.

Expected-return generic method lowering now infers receiver types through
`TypeRef` metadata first, including member-path fallback, and renders the
receiver type only at the method lookup boundary.

C++ match emission now infers the subject through `TypeRef` metadata and passes
that structured type into wrapper-pattern matching, rendering text only for the
enum lookup boundary.

The remaining `parse_expr_text` callers in sema are confined to the explicit
`cpp(...)` escape inference boundary and its member-path string adapter. Normal
Dudu statements and expressions should not route through those helpers; new
compiler work should pass parsed `Expr` nodes instead.

Variable declaration and typed `except` binding names are now validated from
identifier tokens instead of copied from joined statement text, so malformed
bindings such as `player.hp: i32` or `except err.value: Error:` fail in the
parser rather than smuggling raw name strings into later semantic passes.

Tuple literal expression inference now renders from the structured
`infer_expr_type_ast` result instead of manually assembling `tuple[...]` text in
the string compatibility path.

`Ok(...)` and `Err(...)` result-wrapper constructor inference now produces
structured `TypeRef` template nodes in the direct call type path; the older
string inference path renders from that result instead of manually assembling
`Ok[T]`/`Err[T]` text.

Unsupported `def` expressions now parse as a dedicated `DefExpression` AST node
instead of falling through to `Unknown` and being recognized later by raw text
scanning.

Unsupported list/set/dict comprehension syntax now parses as a dedicated
`Comprehension` AST node instead of falling through to `Unknown` and being
recognized later by raw text scanning.

Nonempty `Unknown` expressions are rejected during semantic checking instead of
calling the old raw expression inference path. Unknown expressions also no
longer lower as raw C++ text during C++ emission. Use `cpp(...)` for explicit
native escape hatches.

Optional expression holes now use a dedicated `Missing` expression kind instead
of overloading empty `Unknown` nodes. Parser, sema, codegen, and LSP/lint
helpers should treat `Missing` as absence and reserve `Unknown` for unsupported
source text that must be diagnosed rather than interpreted.
Default-constructed `Expr` values are `Missing`, so optional AST slots stay
absent unless the parser explicitly fills them with real source structure or an
unsupported `Unknown` node.
`expr_missing()` now recognizes only `Missing`; even an empty `Unknown` is an
unsupported expression node, not an absence marker.

The old raw-text compound-assignment normalization fallback has been removed.
Compound assignment is emitted only through the parsed `CompoundAssign` node;
the statement catch-all no longer emits raw statement text for `Unknown`
statements.

Template-call semantic inference now has an AST path that uses parsed template
arguments and parsed runtime arguments. Native/header interop overload checks
also consume parsed runtime argument nodes for ordinary and explicit-template
calls. Unary address-of and dereference expressions are parsed as `Unary` nodes,
which keeps pointer assignment targets such as `*ptr = value` on the AST-backed
assignment path.

Ordinary constructor and explicit-cast call semantic inference now uses parsed
runtime call arguments. Dotted constructor calls are only treated as constructors
when the callee is a known scanned/imported type, so imported C functions such
as `sdl.SDL_PollEvent(...)` do not get misclassified as type construction just
because the name is dotted.

Named-argument call C++ emission now emits from parsed call children. Named call
arguments such as `Point(x=1)` parse as `NamedArg` expression nodes, and
constructor semantic checks consume those nodes directly instead of
rediscovering named fields from raw argument strings. The old raw named-call
rewrite has been removed for parsed calls.
Common dict initializer C++ emission lowers those parsed entries instead of
splitting literal text again.

Built-in calls such as `len(...)`, `range(...)`, `min(...)`, `max(...)`,
`print(...)`, `delete(...)`, and `free(...)` now have an AST semantic inference
path, including arity/type diagnostics for the checked built-ins. Project
helpers such as `align_up(...)` should be ordinary Dudu functions or imported
native functions, not hidden language builtins.

Ordinary C++ call emission now lowers parsed callee and argument nodes directly
instead of rebuilding a raw call string. Built-in primitive casts such as
`i32(value)`, `len(value)`, and pointer cast calls such as
`*struct Native(user_data)` have explicit AST emission paths.
`Ok(value)` and `Err(error)` calls also lower through the parsed call path to
the `dudu::Ok`/`dudu::Err` prelude helpers instead of relying on raw C++ text
rewrites.
Pointer cast calls also have an AST semantic inference path, so native callback
patterns such as `*struct State(user_data)` no longer depend on the raw
expression inference fallback.

Class instance field initializer emission now uses parsed field `value_expr`
nodes instead of raw initializer strings.

Module constants, class constants, static fields, and `static_assert`
declarations now store parsed expression nodes. C++ emission and semantic-token
collection use those declaration expression nodes instead of raw strings.
Decorators now keep parsed expression nodes without a raw text mirror, and
compiler-recognized decorators use a shared helper over those parsed expression
nodes for name matching and first-argument extraction. Decorator expression
parsing uses the same token-piece parser as statements and declarations.
Decorator parsing has regression coverage for string arguments containing
operator characters, such as `@operator("+")`.
Enum value initializers now store parsed expression nodes as well; enum C++
emission and semantic-token collection use those nodes.
Simple integer constant evaluation for `static_assert` failure diagnostics uses
parsed expression nodes for names, integer literals, unary minus, and binary
arithmetic/comparisons instead of reparsing expression strings.
Compile-time expression validation for calls to non-`@constexpr` Dudu functions
walks parsed call/template-call nodes in constants and `static_assert`
declarations instead of scanning raw expression strings.

Dudu-native `@operator("[]")` semantic checks now consume parsed index
argument nodes, including tuple-shaped multi-index expressions, instead of
splitting raw index text.

`range(...)` for-loop C++ emission now reads parsed call arguments from the
iterable expression instead of parsing them back out of lowered C++ text.

Simple indexed assignment semantic checks now use parsed `Index` target
expressions when the indexed base is a local name.

Alias-aware C++ type lowering now keeps parsed `TypeRef` structure for
templates, pointers, references, fixed arrays, function types, and wrapper
types instead of immediately falling back to the raw type string when native
namespace aliases are in scope.

`sizeof[...]`, `alignof[...]`, and `offsetof[...]` semantic checks now use
parsed template `TypeRef` arguments for arity and unknown-type diagnostics.

Unknown statement nodes are rejected before codegen by the unsupported-feature
pass, and the C++ statement emitter now also rejects them if that invariant is
violated. Unsupported syntax must surface as a Dudu diagnostic, not as a
silently dropped statement.

Unknown and deliberately unsupported expression nodes follow the same rule in
the C++ expression emitter. If the unsupported-feature and semantic passes are
bypassed, lambda, conditional-expression, async, generator, invalid slice, and
unknown expression forms still fail as Dudu diagnostics instead of emitting
empty C++ fragments.

Binary expression emission now checks parsed child-expression presence instead
of inspecting child source text before lowering.

The remaining raw C++ expression rewriter is named as an explicit
`cpp(...)` escape lowering helper. Normal AST expression emission keeps the
`lower_cpp_expr_ast` path.

Allocation semantic inference now separates the explicit `cpp(...)` raw-callee
path from parsed `new[T]` and `malloc[T]` template-call semantics, which use
parsed `TypeRef` arguments directly.

Expression-level `cpp(...)` now parses as an explicit `CppEscape` expression
node before ordinary call parsing, and sema/emission strip the escape wrapper at
that node boundary. This keeps native escape hatches visible in the AST instead
of letting them masquerade as normal function calls.
The `CppEscape` expression node stores the extracted C++ body, so sema and C++
expression emission consume the explicit escape payload instead of repeatedly
reparsing the `cpp(...)` spelling.
Statement-level `cpp(...)` nodes also store the extracted C++ body, so C++
statement emission and build-flag validation consume the escape payload rather
than rediscovering it from the statement text.

Index expression C++ emission now lowers parsed base and index child
expressions directly.

List, set, and tuple literal C++ expression emission now uses parsed literal
children instead of raw literal text.

Bare comma expressions now parse as tuple literals, including Python-style
multi-value returns. The shared AST comma splitter is quote-aware so commas
inside string literals do not create phantom tuple elements.

`match` and `case` statements now have explicit `StmtKind` values. The parser
stores the match subject, case pattern text, optional guards, parsed subject
expressions, parsed pattern expressions for simple forms, and parsed guard
expressions. Pattern matching still stops in the unsupported-feature pass, so
the compiler cannot lower a half-implemented match.
Option/Result wrapper and enum case match pattern helpers are shared by semantic
checking and C++ emission, so case names and binding extraction are interpreted
from the same parsed pattern expression shape in both phases.
Match checking and generic type-argument inference now use the shared semantic
failure and source-range helpers, keeping diagnostics anchored through the same
AST node-location path as the rest of semantic analysis.

`super.method(...)` semantic checks use the parsed call callee and argument
nodes. Valid single-base calls lower to explicit C++ base dispatch such as
`Base::method(args...)`; invalid `super` usage gets Dudu diagnostics instead of
falling through to generated C++.

`super.init(...)` semantic checks also use parsed call arguments. A valid
single-base constructor call must be the first statement in `init`; it validates
against the base constructor and emits through the C++ constructor initializer
list instead of lowering as a normal statement.

Statement source ranges now preserve the first and last real statement token
span. This improves diagnostics and editor squiggles without relying on
substring classification. Statement nodes no longer retain whole-statement
source text; explicit statement-level `cpp(...)` escapes store only their
extracted C++ body, and import declarations retain source text for import code
actions. Child expression and type nodes are parsed from token spans and carry
their own source ranges rather than locating semantic subpieces by searching
reconstructed statement text.

## Compiler File Shape

The C++ emitter has been split by AST responsibility:

- `cpp_expr_emit.cpp` keeps expression dispatch.
- `cpp_expr_call_emit.cpp` owns call, template-call, index-hook, and
  enum-constructor expression lowering.
- `cpp_expr_swizzles.cpp` owns swizzle read and assignment expression lowering.
- `cpp_emit_enums.cpp` owns enum forward declarations and full enum emission.
- `cpp_stmt_emit.cpp` owns statement dispatch and non-match statement lowering.
- `cpp_match_emit.cpp` owns match-specific C++ statement lowering and helpers.
- `cpp_stmt_helpers.cpp` owns indentation, escaped string literals, and
  build-time-condition detection.
- `cpp_raw_escape.cpp` owns the explicit raw `cpp(...)` expression escape
  pipeline, keeping that compatibility boundary separate from AST lowering.
- `cpp_raw_escape_templates.cpp` owns type/template-shaped rewrites inside
  explicit `cpp(...)` expressions, such as `new[T]`, `sizeof[T]`,
  `offsetof[T]`, dotted C++ template calls, and `list[T]()` construction.
- `cpp_lower.cpp` now keeps only shared low-level lowering utilities such as
  namespace alias qualification, token-safe string helpers, and template
  argument lowering.
- `cpp_type.cpp` owns raw string type lowering and C/C++ alias cleanup.
- `cpp_type_ref.cpp` owns structured `TypeRef` lowering for parsed type nodes.

Semantic analysis has started the same cleanup:

- `sema_enum.cpp` owns shared enum lookup helpers.
- `sema_common.cpp` owns shared semantic utility helpers.
- `sema_match.cpp` owns wrapper/enum match checking.
- `sema_generics.cpp` owns generic type substitution and generic-call
  inference.
- `sema_super.cpp` owns `super.method(...)` and `super.init(...)` rules.
- `sema_body.cpp` owns function/method body traversal and statement checking.
- `sema_declarations.cpp` owns declaration validation, decorator validation,
  and type-shape checks.
- `sema_inheritance.cpp` owns inheritance relationships, abstract-method
  resolution, and multiple-inheritance declaration rules.
- `sema_expr.cpp` owns core expression inference dispatch.
- `sema_expr_call.cpp`, `sema_expr_template.cpp`, `sema_expr_cpp_escape.cpp`,
  and `sema_expr_support.cpp` own call resolution, template-call inference,
  `cpp(...)` escape inference, and expression-checking support helpers.
- `sema_member_paths.cpp` owns member path walking and field lookup.
- `sema_methods.cpp` owns Dudu method signature lookup.
- `sema_builtin_methods.cpp` and `sema_method_templates.cpp` own builtin C++
  method signatures and method generic substitution helpers.
- `sema_swizzles.cpp` owns Dudu-native swizzle component validation and
  swizzle result type lookup.

Shared `ast_type` helpers now expose first-template-argument extraction for
parsed Dudu `TypeRef` templates. Builtin C++ receiver signatures use that
structural path for Dudu types and keep native `<...>` template extraction in a
small native-spelling fallback for imported C++ types such as
`std::vector<T>` and `std::atomic<T>`.

Statement emission for typed literal initialization now uses parsed `TypeRef`
predicates for `Option`, `array`, `list`, `dict`, and `set` declarations
instead of prefix checks over the rendered type string.

Expression-call emission now classifies pointer receivers, pointer-list
receivers, and wrapped Dudu operator receivers through parsed `TypeRef` nodes
instead of raw leading-character or `list[*...]` checks.

Index and iterable type inference now unwrap pointer types and generic
single-argument containers through parsed `TypeRef` nodes instead of
`*`/`[...]` string slicing. Native/foreign indexable types still return `auto`
at the explicit native boundary.

Emitted-local type inference now recognizes array indexing and pointer
dereference through parsed `TypeRef` nodes, keeping raw native receiver fallback
only in the existing native-spelling boundary.

Semantic expression inference and assignment-target checking now validate
pointer dereference through parsed `TypeRef` nodes instead of raw leading `*`
string checks.

Normal and templated call sema now resolve Dudu-owned static and member calls
from parsed callee/member receiver expressions. The older fallback that split
reconstructed dotted callee strings for method lookup has been removed; dotted
callee spelling is still used only for the native import prefix boundary.
Static generic method lookup uses the same parsed/instantiated method
signature path as instance generic methods, including `class.method[T](...)`.

`@extern_c` C ABI signature checks now classify pointers, references, and
primitive type names through the parsed type tree while preserving the existing
pointer-to-`struct ...` ABI rule.

Pointer arithmetic and native base-class pointer/reference assignability now
classify pointer/reference type shapes through parsed `TypeRef` nodes instead
of leading-character string checks.

Local-address escape analysis now identifies borrowed and pointer locals
through parsed `TypeRef` nodes instead of leading-character type checks.

Parsed pointer-cast expression emission now decides whether a callee is
type-like from the parsed `TypeRef` kind instead of accepting any bracketed raw
string as a type.

Parser construction has been split by grammar responsibility:

- `parser.cpp` owns module orchestration, token cursor helpers, imports,
  decorators, statement blocks, and token joining.
- `parser_decls.cpp` owns declaration grammar for classes, fields, enums, type
  declarations, functions, parameters, constants, and static asserts.
- `parser_internal.hpp` keeps the private parser surface explicit so future
  parser work does not grow one mixed implementation file again.

Native header awareness has been split by scanner responsibility:

- `native_headers.cpp` owns foreign import selection, clang/pkg-config command
  construction, raw scan cache orchestration, and final merge/prefix behavior.
- `native_header_parse.cpp` owns clang AST dump parsing, macro dump parsing,
  and scan dedupe.

Language-server internals have started the same split:

- `language_server_json.cpp` owns the tiny JSON parser/emitter helpers used by
  the LSP transport.
- `language_server_semantic_tokens.cpp` owns AST-backed semantic token
  collection and LSP delta encoding.
- `language_server_diagnostics.cpp` owns compiler diagnostics, lint
  diagnostics, and diagnostic JSON encoding.
- `language_server_support.cpp` owns project config discovery, file URI
  decoding, and shared source-text helpers.
- `language_server_symbols.cpp` owns source/native symbol collection and symbol
  detail strings shared by document symbols, definition, hover, completion, and
  signature help.
- `language_server_symbol_results.cpp` owns document and workspace symbol LSP
  response construction.
- `language_server_hover.cpp` owns hover response construction and doc-comment
  extraction.
- `language_server_local_context.cpp` owns cursor-local type inference, member
  completion target detection, and alias-expanded member candidate types.
- `language_server_completion.cpp` owns completion lists, completion resolve,
  member completion, module completion, and signature help.
- `language_server_definition.cpp` owns go-to-definition for Dudu symbols,
  members, Dudu imports, and native C/C++ header imports.
- `language_server_references.cpp` owns find-references and rename edit
  construction.
- `language_server_navigation.cpp` owns LSP source ranges, locations, symbol
  lookup, and reference scanning helpers.
- `language_server_workspace.cpp` owns workspace and imported-module document
  discovery.
- `language_server_code_actions.cpp` owns format, organize-import, missing
  import, and lint quick fixes.

The previous oversized frontend files have been split below the project
file-size guideline. Future AST/LSP work should keep new responsibilities in
the existing focused language-server files rather than growing
`language_server.cpp` again.

Native interop keeps moving toward parsed metadata instead of raw spelling
guesses:

- C++ `<...>` signature parameter splitting now tracks nested template
  arguments, so signatures such as `vec<L, T, Q>` are parsed as one type.
- scanned C++ function templates preserve Clang's template parameter order, so
  explicit calls bind `choose_second[T, U]` and `make_unique[T]` according to
  the declaration instead of guessed return/parameter usage.
- native overload matching uses parsed `TypeRef` binding for template-shaped
  parameter matching before falling back to compatibility checks.
- native C++ artifact normalization for tuple element/reference types renders
  parsed `TypeRef` children through the shared helper instead of reading raw
  child text directly.
- compiler-recognized decorators can read the full parsed argument list, which
  keeps multi-argument attributes such as `@workgroup_size(8, 8, 1)` intact.
- C struct/class/union/enum tags are normalized during member lookup, so
  `struct stat` fields resolve through the scanned native class named `stat`.
- internal C++ implementation template aliases such as `__detail.__foo[T]`
  are treated as opaque native compiler artifacts at assignment boundaries
  instead of forcing Dudu to model private standard-library alias machinery.
- native enum constants now retain their scanned enum type, while calls into C
  APIs still allow enum constants where an integer parameter is expected.

LSP source edits are also moving to AST-owned ranges:

- import declarations retain whole-statement source ranges and reconstructed
  source text as trivia, so organize-import and missing-import actions can use
  parsed import declarations for import identity, ordering, and insertion
  placement instead of rediscovering imports from line prefixes.
- the old raw document `symbol_at` navigation helper has been removed; editor
  definition, references, and rename entry points resolve cursor symbols through
  AST-backed symbol lookup.
- LSP diagnostic source labels now route from structured error codes such as
  `dudu.parser.*`, `dudu.lexer.*`, and `dudu.sema.*` instead of classifying
  diagnostics by searching human-readable error text.
- Call, method, operator, local function, template call, super call, and
  indexing inference now read `FunctionSignature` return types through the
  structured `return_type_ref` helper first, falling back to native text
  signatures only when imported metadata has no parsed type reference.
- Function and method body checking now carries the parsed return `TypeRef`
  through nested blocks and match cases instead of threading a raw return-type
  string through statement sema.
- The symbol table no longer keeps a separate function-name to return-type
  string map; Dudu functions are represented by `FunctionSignature` metadata
  with parsed parameter and return `TypeRef` fields.
- C++ statement emission now carries function and method return metadata as
  parsed `TypeRef` values through emitted local type inference instead of a
  string return-type map; strings are rendered only at the C++ codegen
  boundary.
- C++ block and match emission now carry the enclosing function return type as
  a parsed `TypeRef` through nested statements, rendering text only when a
  return expression needs generic-method coercion.
- Dudu operator signatures synthesized from methods now preserve parsed
  parameter and return `TypeRef` metadata instead of carrying only rendered
  type strings.
- Function-value typed inference now builds `TypeKind::Function` nodes from
  `FunctionSignature` metadata directly, instead of rendering `fn(...)` text
  and reparsing it.
- Native overload matching now uses the shared structured signature parameter
  helper, so missing parsed refs fall back consistently instead of open-coding
  another params-string parse path.
- Declaration checks for `@test`, operators, constructors, destructors, and
  `@extern_c` now inspect parsed function return `TypeRef` metadata instead of
  comparing rendered return-type strings.
- Typed expression inference for direct non-generic Dudu calls and matched
  native calls now returns `signature_return_type_ref` directly, avoiding the
  old path of rendering a call result type string and reparsing it.
- Typed expression inference for calls through local `fn(...)` values now uses
  parsed `FunctionSignature` metadata directly, so callback variables no longer
  depend on the older string-returning call inference fallback.
- Typed expression inference for normal method calls, static method calls, and
  inferred generic method calls now returns parsed `FunctionSignature` result
  metadata directly; the duplicate string-returning method-call fallback has
  been removed.
- Typed expression inference for explicit template member calls, native
  explicit template calls, and templated known constructors now returns parsed
  `TypeRef` metadata directly; the corresponding string-returning template-call
  fallback branches have been removed.
- Typed expression inference for `super` calls and direct pointer casts now
  returns parsed `TypeRef` metadata directly. Ordinary call inference now stays
  on the typed direct-call path for constructors, built-ins, pointer casts,
  generic functions, local function values, native calls, native-prefix
  fallbacks, methods, and `super`; the old string-returning ordinary call
  inferencer and its unused helper branches have been deleted.
- Template-call inference now stays on the typed direct-template-call path for
  pointer casts, allocation, shape built-ins, explicit generic functions,
  generic constructors, native explicit template calls, template methods, and
  native-prefix fallbacks; the old string-returning template-call inferencer has
  been deleted, with unsupported/unknown diagnostics owned by expression sema.
- Condition and comparison-operator semantic checks now validate operator
  return types through parsed `signature_return_type_ref` metadata instead of
  comparing rendered signature return strings.
- Function body assignment checks now expose a typed assignment callback, so
  declared-value checks and inferred generic method return checks can compare
  expected `TypeRef` metadata before falling back to compatibility text.
- Return statements and missing-return checks now decide `void` through parsed
  return `TypeRef` metadata, using rendered text only for diagnostics.
- Compiler-owned built-in C++ method signatures now populate parsed parameter
  and return `TypeRef` metadata when created, instead of relying on later
  fallback parsing of signature strings.
- Native function signatures now attach parsed parameter and return `TypeRef`
  metadata during symbol collection, narrowing fallback parsing to the native
  declaration AST boundary that still stores imported signatures as text.
- `NativeFunctionDecl` now carries parsed parameter and return `TypeRef`
  metadata directly, and native header scans plus module-imported function
  aliases populate those refs at the AST boundary.
- Function and method C++ emission now seeds statement blocks with parameter
  `TypeRef` maps directly from `ParamDecl` metadata, instead of forcing the
  statement emitter to reconstruct initial locals by reparsing parameter type
  strings.
- Explicit native template return rewriting now refreshes return `TypeRef`
  metadata at the same time as return text, avoiding stale semantic facts after
  helpers such as indexed tuple/variant returns refine the type.
- Explicit native template binding now treats native index placeholders as
  numeric-only, so type-form calls such as `std.get[i32](variant_value)` do not
  accidentally bind helper alias index placeholders before real type
  placeholders.
- Explicit native type-template calls whose imported C++ return type remains a
  dependent helper alias now derive the semantic return from the explicit type
  argument while preserving parsed reference/const wrappers.
- Explicit native template fallback signatures now synthesize parsed `auto`
  return metadata instead of text-only return signatures.
- Codegen local-type inference now exposes a parsed `TypeRef` result path, so
  inferred `auto` locals and generic method argument matching can preserve
  structured type metadata instead of rendering inferred type strings and
  immediately parsing them back.
- Expected-type generic method lowering now accepts parsed `TypeRef` metadata,
  so declared local initializers can infer generic method template arguments
  without reparsing the declared type text.
- Typed expression inference now handles direct generic Dudu function calls by
  instantiating the generic signature and returning its parsed `TypeRef`,
  avoiding the older render-return-type-then-parse fallback for those calls.
- Return statements and assignment to existing locals now pass parsed return
  and local `TypeRef` metadata into expression coercion when available, leaving
  the string coercion wrapper only for target types that still have no
  structured metadata.
- Explicit generic Dudu template-call validation now lives in a shared
  signature helper, and typed expression inference returns the instantiated
  signature `TypeRef` directly for those calls instead of routing through
  rendered return-type text.
- Typed direct-call and template-call inference now lives in a focused
  expression type-call module, keeping the main expression inference file from
  accumulating more compatibility migration code.
- Binary expression typed inference now has a focused operator module that
  returns parsed `TypeRef` results for overloaded operators and contextual
  numeric literals, instead of forcing typed callers through rendered operator
  return strings.
- Assignment target checking now exposes a parsed `TypeRef` result path and
  sema body checks use it for normal and compound assignments, deleting the
  older string-only `check_type_match` wrapper.
- Index result inference now exposes parsed `TypeRef` APIs, and typed
  expression inference plus indexed assignment target checks use them instead
  of owning their own indexed-type text parsing.
- Structured index result inference now handles alias resolution,
  foreign/auto receivers, and Dudu `[]` operator return metadata inside the
  `TypeRef` path, removing the render-to-string and reparse fallback from
  normal `indexed_type_ref_from_type` calls.
- Structured iterable inference now extracts element `TypeRef` results from
  parsed local metadata or one parsed declared local type, instead of routing
  through the older string-returning iterable helper and reparsing the rendered
  element type.
- Assignment target inference now exposes parsed wrapper-child helpers and a
  swizzle assignment `TypeRef` path, so pointer dereference and swizzle write
  targets no longer parse rendered target type strings.
- Member-path type inference now resolves local names, class static members,
  recursive member fields, indexed member receivers, inherited fields,
  `Result` helper fields, and Dudu swizzles through parsed `TypeRef` metadata
  before rendering fallback display text. The string member resolver remains
  for explicit C++ escape and C++ emission boundaries only.
- The older string-recursive member resolver and string class-field helper have
  been deleted; compatibility callers now render the result of structured
  `TypeRef` member/field inference instead of maintaining a parallel semantic
  path.
- Direct member expression typing now asks field and swizzle helpers for
  structured `TypeRef` results, removing the previous string result and
  immediate reparse step for `value.field` and `value.xy` style expressions.
- C++ expression emission now also uses structured member and swizzle type
  helpers for deciding Dudu field access and swizzle result construction,
  instead of querying those semantics through rendered type strings.
- C++ call emission now detects pointer-typed member receivers through
  structured member `TypeRef` metadata when resolving `value.method(...)`
  lowering, leaving text parsing only for the older string-local map boundary.
- Statement codegen now routes expression emission through a typed overload
  that carries `local_type_refs`; member field decisions, swizzle assignment
  lowering, array literal recursion, and `[]=` operator hook lookup can use
  structured local metadata instead of relying only on rendered local type
  strings.
- The typed expression-emission path now reaches call, callee, swizzle, and
  matrix/slice helper recursion, so nested lowering preserves `local_type_refs`
  through method-call receiver decisions, swizzle receiver typing, and sliced
  index expressions.
- Shared argument-list lowering now also has a typed path. Template calls,
  collection literals, enum payload constructors, pointer casts, `offsetof`
  fallback fields, named arguments, tuple-shaped indexes, and expected generic
  method calls keep `local_type_refs` when they recursively lower child
  expressions.
- Decorator helpers now render names and non-string arguments from parsed
  expression nodes through the shared AST display/path helpers instead of
  trimming raw decorator expression text.
- Function signature display now renders parameters and return types through
  `TypeRef` helpers, leaving string signature fields as native-interop mirrors
  rather than the primary semantic source.
- Match C++ emission now lowers subject, guard, and simple switch pattern
  expressions through the typed expression path, so payload-bound locals in
  guarded cases keep parsed `TypeRef` metadata during codegen.
- Local swizzle C++ emission now passes parsed local `TypeRef` metadata into
  the local-class fast path instead of reparsing the local type string before
  asking semantic swizzle helpers for the result type.
- Function-scope local type lookup now has a shared `TypeRef` helper that
  prefers parsed local metadata and names the old local string map as an
  explicit compatibility fallback. Normal expression sema and explicit
  `cpp(...)` pointer/address escape inference now use it.
- Statement-codegen local type inference now has the same shape: local names
  and direct indexed-local inference ask a shared helper for parsed local
  `TypeRef` metadata first, with the rendered local type string kept as a
  named compatibility fallback.
- Member expression type lookup now uses the same typed-first local lookup
  shape, so local member-path sema asks parsed `TypeRef` metadata before the
  legacy local type string mirror.
- Native value symbols now store parsed `TypeRef` metadata beside their C++
  spelling strings, and normal name/member expression sema reads those refs
  directly for imported constants, build flags, shader/native values, and class
  constants instead of reparsing native value type text.
- AST assignment compatibility now resolves aliases through parsed `TypeRef`
  metadata when either side is already structured. The pure string overload is
  kept as a named compatibility boundary for older callers.
- Member-path receiver unwrapping and `Result[...]` field lookup now resolve
  aliases through parsed `TypeRef` metadata first, with the old alias string map
  used only as a compatibility fallback.
- Indexed type inference now also resolves aliases through parsed `TypeRef`
  metadata before unwrapping pointer/reference/container/array shapes, keeping
  aliases to list/span/array-like types off the string-only path.
- Native template signature binding now resolves compatibility `got` types
  through parsed alias `TypeRef` metadata before binding placeholders, with the
  string alias map retained only as a fallback for older callers.

Expression parsing has moved onto the lexer/token stream:

- `parse_expr_text` now lexes the expression text and delegates to a token
  parser, instead of classifying top-level expression forms by substring scans.
- The token parser preserves structured nodes for literals, unary/binary
  operators, calls, template calls, members, indexes, tuples, lists, dicts,
  sets, slices, named arguments, explicit `cpp(...)` escapes, and unsupported
  Python expression forms that need precise diagnostics.
- This is a compatibility migration point. The public `parse_expr_text` entry
  remains while statement/declaration parsing still passes expression spans
  through it. Future parser work should pass token spans directly. The token
  expression parser implementation has been split into a small public entry
  file, core precedence/prefix parsing, and postfix/primary parsing so new
  expression work does not grow another oversized compiler file.

## Acceptance

- Existing examples still compile and run.
- Generated C++ remains readable.
- Parser errors point at exact tokens.
- Type errors point at exact expressions.
- LSP semantic tokens distinguish types, locals, fields, methods, params, and
  native symbols.
- `duc check` can diagnose common mistakes without relying on generated C++
  failure output.
- AST lowering covers the forms used by the generic and macro target examples.
