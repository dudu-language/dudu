# Public Readiness Plan

This is the near-term execution plan for making Dudu feel like a coherent first
public language version.

This plan does not replace [Le Plan](le_plan.md). It selects the next work from
that broader roadmap and orders it around one concrete goal: a user should be
able to try Dudu, import real Dudu libraries, write Python-shaped native code,
use C/C++ libraries, and get useful compiler/editor feedback without seeing the
prototype seams.

## Bar

This is not a "first slice" plan. Each section should end with:

- implementation that follows normal Dudu semantics
- deterministic compiler tests in this repo
- runnable examples or dogfood projects where useful
- LSP/editor fixtures when the feature affects editor behavior
- docs that describe what shipped and what is intentionally not supported
- no tensor/library-name special cases in compiler code
- no compatibility path for unreleased syntax
- no hidden rank-2, row/column, or package-specific shortcut that could not
  generalize to another user library

If a feature cannot be implemented cleanly, stop and document the missing
compiler architecture before adding a workaround.

Primary references:

- [Indexing Dispatch Model](indexing-dispatch-model.md)
- [Arrays And Indexing Plan](arrays-indexing-plan.md)
- [Tensor Backend And Numeric Stack Plan](tensor-backend-plan.md)
- [Generics Plan](generics-plan.md)
- [Editor Intelligence Plan](editor-intelligence-plan.md)
- [Import Semantics](import_semantics.md)
- [Project Driver Plan](project-driver-plan.md)
- [Le Plan](le_plan.md)

## 1. Finalize Indexing And Shaped Generic Semantics

Goal: finish the Dudu-language side of Python-shaped indexing and shaped tensor
APIs so a third-party library can implement NumPy/PyTorch-like behavior without
compiler help.

Already implemented and should stay protected by tests:

- structured index items: scalar expression, `slice`, `ellipsis`, `new_axis`
- fixed-array scalar and slice indexing through rank-independent `array_view`
- library `@operator("[]")` and `@operator("[]=")` dispatch
- fixed-arity and variadic index hooks
- `basic_index` / `scalar_index` category matching
- direct advanced indexing through library-defined index object types
- shaped metadata such as `Tensor[f32][dyn, 784]`
- shape/value generic arithmetic in shaped metadata, including expressions such
  as `B, C * H * W`
- arbitrary-rank reference fixtures through `tests/targets/tensor_indexing`

Remaining language work:

- broaden shape-arithmetic fixtures from flatten-style products into matmul,
  reshape, conv-like output shape, and fixed-size vector/matrix math
- improve rejected shape-expression diagnostics so unsupported operators,
  malformed expressions, and compile-time requirements point at Dudu source
- make scalar extraction policy explicit: one-element tensor/view results use
  `.item()` unless and until rank/pack-count constraints can prove scalar
  return safely
- audit compiler source for old rank-specific array/tensor shortcuts and remove
  any remaining shortcut

Required fixtures:

- positive shape-arithmetic fixtures: matmul, flatten, reshape, conv-like output
  shape, fixed-size vector/matrix math
- negative shape-arithmetic fixtures: incompatible symbolic dimensions,
  unsupported arithmetic, `dyn` used where a compile-time value is required
- no-regression target manifest for `tests/targets/tensor_indexing`
- generated C++ inspection checks for shaped generics where practical

Acceptance:

- `scripts/check_targets.sh` passes with shape-arithmetic additions
- no compiler branch recognizes tensor package/type names
- shape diagnostics mention both expected and actual symbolic/concrete
  dimensions when known

## 2. Bottle The Numeric Library Proof

Goal: prove the language can hide numeric/backend machinery behind normal Dudu
libraries, without making those libraries part of the language or standard
library.

Target packages:

- `ndad`: ndarray/tensor storage, indexing, shape metadata, CPU backend,
  optional BLAS/OpenCL backend modules, reductions, broadcasting, view/copy
  boundaries
- `mald`: small PyTorch-like autograd/module/optimizer layer built on top of
  `ndad`
- `ddtorch`: optional facade name if a more PyTorch-shaped API is useful later

User-facing target:

```python
from ndad import Tensor
from ndad import randn
from ndad.backends import openblas

x = randn[f32](32, 784).to(openblas.default())
w = randn[f32](784, 10).to(openblas.default())
logits = x.matmul(w)
row = logits[0, :]
```

Rules:

- user examples import `ndad` / `mald`, not internal helper headers
- native helper headers may exist inside the library implementation
- BLAS/OpenCL backends are optional modules, not required for CPU-only tensor use
- the compiler test suite remains the source of truth for language behavior
- dogfood repos prove usability and packaging, not hidden compiler semantics

Concrete work:

- extract the reusable parts of `dudu-datascience/src/dudu_tensor.dd` into an
  ordinary package-shaped `ndad` prototype
- remove direct user exposure of `dudu_tensor/index_native.hpp`-style helpers
- make CPU-only examples work without linking OpenCL
- keep OpenBLAS/OpenCL demos as optional backend examples
- make a tiny `mald` target with basic autograd if compiler features are
  sufficient; if not, document the missing language feature precisely

Acceptance:

- sample user project imports `ndad` normally and runs CPU-only tensor code
- optional backend sample imports `ndad.backends.openblas` and runs matmul
- optional backend sample imports `ndad.backends.opencl` only when OpenCL is
  available
- no user-facing example needs native helper headers

## 3. Improve Tensor/Generic Editor Intelligence

Goal: make advanced Dudu code explain itself in the editor like a serious
compiled language.

Primary reference:
[Editor Intelligence Plan](editor-intelligence-plan.md#tensor-and-shape-intelligence).

Required behavior:

- hover on shaped values shows full type, shape metadata, and known backend or
  layout facts when honest
- hover on `tensor[...]` shows selected `@operator("[]")` overload and whether
  the result is a view, owning tensor, scalar-like selection, or unknown library
  policy
- inlay hints expose inferred tensor/view shapes at locals and loop bindings
- parameter inlay hints work for Dudu functions, methods, constructors,
  builtins, and native C/C++ functions when parameter names are available
- inlay hints are hoverable and type label parts are clickable when the target
  is resolvable
- no LSP code should special-case `ndad`, `mald`, `Tensor`, or backend names

Required fixtures:

- LSP hover fixtures for shaped Dudu types
- inlay hint fixtures for shaped locals, loop bindings, and function calls
- native parameter-name hints for scanned C/C++ functions
- go-to-definition from hint label parts where the client/test harness can
  validate it
- dogfood checks in `raymarch-dd`, `dudu-webserver`, and the numeric sample

Acceptance:

- warm hover/definition/inlay requests are fast enough for interactive editing
- editor output is derived from AST/sema/native metadata, not text regexes
- unresolved or unsupported shape facts report honestly instead of guessing

## 4. Improve Generic And Index Diagnostics

Goal: make failed indexing/generic code actionable.

Primary reference:
[Editor Intelligence Plan](editor-intelligence-plan.md#generic-and-index-diagnostic-ux).

Required behavior:

- failed `[]` / `[]=` overloads show candidate signatures and rejection reasons
- diagnostics distinguish fixed-array indexing from library hook indexing
- diagnostics name `basic_index`, `scalar_index`, `slice`, `ellipsis`,
  `new_axis`, and user-defined advanced index types in Dudu terms
- shape mismatches show expected and actual symbolic/concrete dimensions
- missing variadic hook and bad assignment value type errors include a useful
  hint
- LSP diagnostics reuse compiler diagnostic data rather than reconstructing
  errors independently

Required fixtures:

- bad fixed-array index count
- bad multiple ellipsis
- bad `new_axis`/`ellipsis` into fixed-arity hooks
- bad `Mask` into `basic_index`
- bad scatter value type
- bad concrete/dyn shape narrowing
- bad shape arithmetic expression
- overload set where one candidate is close and another is impossible

Acceptance:

- target negative fixtures fail for the intended diagnostic substring
- candidate reasons are stable enough to test without being brittle wall text
- errors do not fall through to C++ template spew for normal Dudu mistakes

## 5. Implement Dudu Dependency Bootstrapping

Goal: let early Dudu libraries be consumed before a central package registry
exists.

Primary reference:
[Project Driver Plan](project-driver-plan.md#dudu-dependencies-and-lockfiles).

Decision:

- support Git/path dependencies in project metadata
- do not put URLs in source imports
- source imports stay logical and stable: `from ndad import Tensor`
- native C/C++ dependencies remain CMake/pkg-config/system/vendor concerns

Target manifest:

```toml
[deps]
ndad = { git = "https://github.com/wegfawefgawefg/ndad.git", tag = "v0.1.0" }
mald = { git = "https://github.com/wegfawefgawefg/mald.git", rev = "..." }
local_math = { path = "../local_math" }
```

Target lockfile:

- records package name
- records source kind: `path`, `git`, future `registry`
- records exact Git commit for Git deps
- records resolved package root and package metadata
- is regenerated by explicit update/fetch actions, not silently churned on every
  build unless a dependency is missing

Near-term command behavior:

- `dudu build` resolves/fetches missing Dudu source deps before compile
- `dudu run` and `dudu test` use the same dependency resolution
- `dudu deps fetch` or equivalent fetches/updates without building
- path deps work first because they are essential for local dogfood
- Git deps support `tag`, `rev`, and `branch`; `rev`/`tag` are preferred for
  reproducibility
- diagnostics clearly separate Dudu source deps from native C/C++ deps

Acceptance:

- a sample project depends on local `ndad` through `[deps] path`
- a sample project depends on Git `ndad` through `[deps] git`
- imports from dependency modules resolve in compiler and LSP
- lockfile pins Git deps and is stable across no-op builds
- missing Git, auth failure, bad tag, missing package root, and native dependency
  failure all produce different useful diagnostics

## 6. Public Example And Validation Matrix

Goal: make the public demo story honest and reproducible.

Required examples:

- hello project from `dudu new`
- C/C++ native import smoke
- raylib or SDL app if local packages are available
- `raymarch-dd`
- `dudu-webserver`
- CPU-only `ndad` tensor sample
- optional OpenBLAS backend sample
- optional OpenCL backend sample
- pending/optional `mald` autograd sample

Required validation commands:

- fast compiler tests
- negative compiler tests
- target manifest checks
- LSP matrix checks
- dogfood checks
- optional native probes
- package/dependency smoke checks

Acceptance:

- default validation stays fast
- optional/heavy probes skip clearly when dependencies are missing
- examples do not depend on private local paths except explicit dogfood paths
- public README and docs describe what is complete, optional, and future work

## Execution Order

1. Shape/value generic arithmetic and diagnostic fixtures.
2. Generic/index overload diagnostics.
3. Tensor/shape LSP hover and inlay plumbing.
4. Path dependencies and lockfile skeleton.
5. Git dependencies.
6. Bottle `ndad` as an ordinary package-shaped prototype.
7. Add optional backend examples and package smoke tests.
8. Re-run cleanup/style pass and remove any temporary compatibility hooks.
9. Update public docs/README around actual shipped behavior.

This order keeps core language guarantees ahead of library packaging, while
still ending with a user-visible library proof.
