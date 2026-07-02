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
- shape-arithmetic API fixtures for flatten, reshape, conv-like output shape,
  matmul, and matrix-vector shape signatures
- arbitrary-rank reference fixtures through `tests/targets/tensor_indexing`
- scalar extraction policy is explicit: one-element tensor/view results use
  `.item()` unless rank/pack-count constraints can prove scalar return safely
- fixed-array and tensor indexing shortcuts were audited against
  [Indexing Dispatch Model](indexing-dispatch-model.md); fixed arrays use the
  generic `array_view[T]` path and tensor indexing uses operator hooks
- shape diagnostics cover folded mismatches, unsupported operators, and `dyn`
  used inside compile-time arithmetic

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
- make a tiny `mald` target with basic autograd

Acceptance:

- sample user project imports `ndad` normally and runs CPU-only tensor code
- optional backend sample imports `ndad.backends.openblas` and runs matmul
- optional OpenCL sample stays isolated from CPU-only `ndad` imports until
  Dudu has clean extension-module boundaries for backend-specific methods
- `mald` sample imports normal Dudu packages and runs a tiny training loop
- no user-facing example needs native helper headers

Status: `/home/vega/Coding/ML/dudu-datascience` now bottles the CPU/OpenBLAS
surface as `ndad`, keeps OpenCL in the older optional `dudu_tensor` target, and
graduates a small `mald` autograd/training layer on top of `ndad`.
`DUDU_BIN=/home/vega/Coding/LangDev/dudu/build/dudu ./scripts/check_target_api.sh`
passed on 2026-07-02 with 5 graduated specs and 0 pending specs.

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

Status: Dudu operator hover now reports the selected `@operator(...)` overload
and result type for indexed/operator expressions, while preserving any attached
method docs. Inlay type labels for imported shaped classes now expose hoverable
definition previews and clickable label parts that jump back to the imported
module source, including module-qualified labels such as `tensor.Tensor`.
Parameter-name inlay hints for Dudu functions, methods, constructors, and
scanned native C/C++ functions now include hoverable type tooltips when the
callee metadata exposes parameter types.

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

Status: failed Dudu `@operator("[]")` and `@operator("[]=")` dispatch now
reports candidate signatures and per-candidate rejection reasons. The tensor
target manifest checks missing variadic hooks, fixed-arity `ellipsis` and
`new_axis` rejection, fixed-array arity, multiple ellipsis, scatter assignment
value type, symbolic/concrete shape mismatches, `dyn` arithmetic rejection, and
an explicit overload set where one candidate fails by arity and another fails by
argument type. The formatter is implemented in `sema_operator_diagnostics.cpp`
so the normal operator resolver remains separate from diagnostic presentation.
Direct LSP diagnostics now have a fixture proving the same failed-index
candidate reasons reach editor diagnostics from the compiler diagnostic path.

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

Status: `[deps]` now accepts path and Git source dependencies in
`dudu.toml`. `dudu deps fetch` resolves dependencies and writes `dudu.lock`;
`dudu build`, `dudu run`, `dudu check`, `dudu cmake`, and `dudu test` resolve
missing deps through the project driver before indexing/building. Path deps are
resolved relative to the manifest, package source roots prefer `src/` when it
exists, and Dudu imports stay logical, for example
`from local_math import value`. Git deps clone into `.dudu/deps/<name>`, support
`rev`, `tag`, or `branch`, and record the resolved commit in the lockfile.
Dependency smoke tests cover successful path/Git dependencies, lockfile
stability, missing paths, existing folders that are not Dudu package roots, bad
Git refs, and native pkg-config failures as distinct diagnostics.

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
- `mald` autograd/training sample

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

Status: [Validation Matrix](validation-matrix.md) now defines the fast default
suite, editor/LSP checks, optional native probes, dogfood checks, numeric stack
checks, and release-candidate checks. `scripts/test_dependencies.sh` covers the
public Dudu source dependency path with a local path package, a local Git
package, logical imports, and lockfile stability without requiring network.
Public examples have been migrated to the canonical Python-shaped native import
syntax from [Import Semantics](import_semantics.md), including `.path` imports
for local/vendor headers and unaliased `from cpp import thread`-style C++
standard-library imports. `scripts/test_examples.sh` validates the public
example set and skips optional native packages clearly when they are not
installed. Native alias codegen now distinguishes C tag types from typedef
aliases, so examples such as `from c import sys/stat.h as stat` can write
`stat.stat` and still emit `struct stat`, while typedef aliases keep their
typedef spelling. The OpenCV image example intentionally uses API-level OpenCV
interop (`cv.cvtColor`, `cv.Canny`, `cv.bitwise_not`) instead of wrapper
headers or unsupported direct native template field writes.

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
