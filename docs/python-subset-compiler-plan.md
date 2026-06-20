# Python Subset Compiler Plan

This is the implementation plan for the new Dudu goal:

```text
A statically typed subset of Python that compiles to readable C++,
has direct access to C and C++ libraries, and emits normal C++ headers
so Dudu code can be used from C++.
```

## Current Inputs

Primary design docs and fixtures:

- [Appearance Spec](appearance-spec.md)
- `examples/*.dd`

## Product Goal

Dudu should feel like typed Python:

```python
def add(a: i32, b: i32) -> i32:
    return a + b
```

But compile like C++:

```cpp
int32_t add(int32_t a, int32_t b) {
    return a + b;
}
```

The project should avoid preserving Python runtime semantics when those
semantics fight C++ output. Dudu is Python-shaped, not CPython-compatible.

## Language Baseline

Implement this first:

- `.dd` files with Python-like indentation.
- Python comments.
- `import`, `import c "..." as name`, `import cpp "..." as name`.
- qualified module imports by default.
- selective imports with compile errors on direct-name collisions.
- facade modules through ordinary imports, with no special reexport system.
- `class` with typed fields and methods.
- `enum`.
- `type Name = Type`.
- `def name(args...) -> Return:`.
- Python calls with parentheses and commas.
- `return`, `if`/`elif`/`else`, `while`, `for`, `break`, `continue`.
- labeled `for`/`while` loops with `break label` and `continue label`.
- typed local annotations: `x: i32 = 0`.
- local type inference from clear initializers.
- constants via mandatory `ALL_CAPS` bindings.
- compile-time build flags through `build.NAME`.
- static assertions with `static_assert(...)`.
- compile-time functions with `@constexpr`.
- tuple return and destructuring assignment.
- `Result[T, E]` and `Option[T]`.
- C++ exception interop through `try`, `except`, and `raise`.
- typed loop variable extension:
  ```python
  for x: &Thing in things:
      ...
  ```
- no implicit casts across Dudu-native assignments and returns.

Named function declarations replace Python `lambda`. A `def name(...)`
declaration is statement-only at module scope or class scope. Function names are
values after declaration, so callbacks and tables use ordinary named functions
instead of inline function expressions. Nested local `def` declarations are not
part of the function-body grammar.

Rejected Python expression sugar:

- `lambda`
- `def` expressions
- ternary conditional expressions
- list, dict, and set comprehensions
- generator expressions
- `with` context-manager statements

Use explicit statements and loops instead. This keeps allocation, capture,
lifetime, and control flow visible in systems code.

## Type Surface

Built-ins:

```text
bool
char
i8 i16 i32 i64
u8 u16 u32 u64
isize usize
f32 f64
void
str
cstr
```

Dudu-native source uses the fixed-width names above. It does not provide
`int`, `float`, or `double` as source-level aliases. Imported C/C++ headers can
still contain those spellings; the header scanner maps them into Dudu types for
semantic checks and editor tooling.
`char` is reserved for native C/C++ character and mutable `char *` buffer
interop; normal byte-oriented Dudu code should use `i8` or `u8`.

Containers:

```python
values: list[i32]
names: dict[str, i32]
seen: set[str]
pair: tuple[i32, f32]
static_values: array[i32][256]
matrix: array[f32][4, 4]
```

Native low-level types:

```python
p: *i32
r: &i32
pc: *const[i32]
rc: &const[PlayerState]
```

Allocation helpers:

```python
p: *Player = new[Player](100, "bob")
delete(p)

bytes: *u8 = malloc[u8](1024)
free(bytes)
```

Value containers own values:

```python
def make_players() -> list[Player]:
    players: list[Player] = []
    players.append(Player(100, "bob"))
    return players
```

Pointer containers are non-owning unless a library type says otherwise:

```python
borrowed: list[*Player]
```

Custom allocators are library APIs, not core language semantics:

```python
enemy: *Enemy = arena.make[Enemy](spawn)
arena.reset()
```

Imported C++ templates:

```python
items: std.vector[i32]
table: std.unordered_map[str, PlayerState]
ptr = std.make_unique[PlayerState](10, 20)
```

## Removed Python Semantics

Reject:

- Python `finally`
- `eval`, `exec`
- monkeypatching modules/classes/functions
- dynamic attribute creation on typed classes
- metaclasses and descriptors
- arbitrary `getattr`/`setattr` by runtime string
- rebinding a local to a different type
- arbitrary CPython package imports
- generators and `yield`
- `async` and `await`
- multiple inheritance
- runtime class creation
- decorators, except compiler-recognized attributes
- heterogeneous containers unless explicitly typed dynamic object support exists
- arbitrary precision Python `int` as the default integer model

## Compiler Architecture

Split the current compiler before adding the new parser.

Target layout:

```text
src/
    main.cpp
    diagnostics.hpp/.cpp
    source.hpp/.cpp
    lexer.hpp/.cpp
    tokens.hpp
    ast.hpp/.cpp
    parser.hpp/.cpp
    resolver.hpp/.cpp
    types.hpp/.cpp
    checker.hpp/.cpp
    cpp_emit.hpp/.cpp
```

Keep files near the repo's 300-500 line target where practical.

## Stage 1: Replace The Parser

Do not mutate the old line-oriented parser forever. Add a real lexer/parser for
the Python-shaped syntax.

Lexer requirements:

- indentation tokens: `indent`, `dedent`, `newline`
- names, keywords, string literals, numeric literals
- punctuation: `()[]{},.:`
- operators: `=`, `+=`, `-=`, `*=`, `/=`, comparisons, arithmetic, `&`, `*`
- comments and blank lines

Parser requirements:

- module imports
- foreign imports
- import aliases
- selective imports
- classes
- enums
- aliases
- functions
- statements
- expressions
- type expressions

Parsing target examples:

- `examples/numerics_kmeans.dd`
- `examples/cpp_library.dd`
- a tiny hello fixture

Name-resolution requirements:

- `import foo.bar` binds `foo` and requires qualified access such as
  `foo.bar.Name`.
- `import foo.bar as fb` binds `fb`.
- `from foo.bar import Name` binds `Name` in the current module.
- `from foo.bar import Name as Alias` binds `Alias`.
- direct-name collisions are compile errors.
- facade modules are normal modules that import and expose selected names.
- generated C++ namespaces mirror Dudu module paths.

## Stage 2: AST And Type AST

Represent Python-shaped syntax directly.

Important AST forms:

- `Module`
- `Import`
- `ForeignImport`
- `ClassDecl`
- `EnumDecl`
- `TypeAlias`
- `FunctionDecl`
- `Block`
- `IfStmt`
- `WhileStmt`
- `ForStmt`
- `ReturnStmt`
- `AssignStmt`
- `DestructureAssignStmt`
- `ExprStmt`
- `CallExpr`
- `MemberExpr`
- `IndexExpr`
- `UnaryExpr`
- `BinaryExpr`
- `TupleExpr`
- `ListLiteral`
- `DictLiteral`
- `SetLiteral`

Type AST forms:

- named type: `i32`, `PlayerState`, `glm.vec3`
- generic type: `list[i32]`, `Result[T, E]`
- array shape: `array[i32][256]`, `array[f32][4, 4]`
- pointer: `*T`
- reference: `&T`
- const: `const[T]`
- function pointer: `fn(T, U) -> R`

## Stage 3: Minimal Typechecker

Typecheck Dudu-native code before serious interop.

Implement:

- scalar type lookup
- class type lookup
- enum type lookup
- type aliases
- function signatures
- local scopes
- Dudu-native naming rules, including all-caps constants
- fields and member access for Dudu classes
- function calls
- constructor calls
- basic operators
- list/dict/set/tuple literals where annotated
- tuple destructuring count/type checks
- typed `for` variables
- no implicit casts in Dudu-native assignments/returns
- `Result`/`Option` as known generic types
- imported C++ calls preserve native syntax so the C++ compiler resolves
  overloads

## Stage 4: C++ Emission

Emit readable C++ first.

Mappings:

```text
i32                 -> int32_t
u64                 -> uint64_t
f32                 -> float
str                 -> std::string
list[T]             -> std::vector<T>
dict[K, V]          -> std::unordered_map<K, V>
set[T]              -> std::unordered_set<T>
tuple[...]          -> generated aggregate by default
array[T] = literal  -> fixed contiguous storage with inferred shape
array[T][N]         -> fixed contiguous storage
array[T][M, N]      -> fixed contiguous matrix/tensor storage
*T                  -> T*
&T                  -> T&
const[T]            -> const T
fn(A, B) -> R       -> R (*)(A, B)
Result[T, E]        -> dudu::Result<T, E>
Option[T]           -> std::optional<T>
new[T](...)         -> new T(...)
delete(p)           -> delete p
malloc[T](n)        -> typed malloc helper
free(p)             -> free helper
```

Emit:

- `.cpp` for executable targets
- `.hpp` plus `.cpp` for library targets
- top-level functions as normal C++ functions
- classes as C++ structs/classes with public fields initially
- tuple returns as generated aggregate structs
- destructuring as C++ structured bindings

Example:

```python
def divmod_i32(a: i32, b: i32) -> tuple[i32, i32]:
    return a / b, a % b
```

Should emit something like:

```cpp
struct divmod_i32_result {
    int32_t _0;
    int32_t _1;
};

divmod_i32_result divmod_i32(int32_t a, int32_t b);
```

## Stage 5: Runtime Prelude

Add a tiny C++ prelude for Dudu-generated code.

Needed early:

- `dudu::Result<T, E>`
- `Ok(value)`
- `Err(error)`
- perhaps helper casts or checked conversion utilities

Prefer header-only at first:

```text
runtime/dudu.hpp
```

Keep it small and inspectable.

## Stage 6: C Interop

Emit-only interop first:

```python
import c "stdio.h" as c
```

Generated C++ includes the header and lowers dot paths:

```cpp
#include <stdio.h>
c.printf(...)
```

For C libraries, dot aliasing may require wrapper namespaces or direct textual
lowering. The first implementation can support:

- include emission
- alias-to-global lowering for C functions/constants
- pointer arguments
- simple structs/types if users spell imported type names

Add local fixtures:

```text
tests/fixtures/interop_c/
    dudu_c_math.h
    dudu_c_math.c
    main.dd
```

## Stage 7: C++ Interop

Emit-only C++ interop first:

```python
import cpp "glm/glm.hpp" as glm
import cpp "memory" as std
```

Generated C++ includes headers and lowers dot paths to C++ namespaces/members.

Support in first pass:

- namespaces
- free function calls
- constructors
- public fields
- member calls
- imported template spelling `std.vector[i32]`
- operator expressions that C++ can compile

Use the C++ compiler as the real validator until Clang import lands.

Add local C++ fixtures:

```text
tests/fixtures/interop_cpp/
    dudu_cpp_vec.hpp
    dudu_cpp_vec.cpp
    main.dd
```

## Stage 8: Clang-Backed Import

Full C++ library access requires Clang tooling.

Do not hand-parse C++ headers.

Use Clang/libclang/clang tooling to inspect:

- functions
- overload sets
- namespaces
- records/classes
- methods
- constructors
- enums
- typedefs/using aliases
- templates
- constants and simple macros

This stage enables better diagnostics and Dudu-side overload resolution for
foreign declarations. Generated C++ remains the source of truth for imported
constructors, methods, templates, and operators.

## Stage 9: Build Tool

Use `duc` as the compiler driver and `dudu` as the project driver.

Expected commands:

```sh
duc emit src/main.dd -o build/main.cpp
duc run src/main.dd
duc fmt
duc src/main.dd --emit-cpp -
dudu build
dudu run
dudu test
dudu clean
```

Use normal C/C++ build tools:

- direct `clang++`/`c++` compile for simple files
- CMake integration for projects
- `pkg-config` probes for common native libraries
- preserve `compile_commands.json`

## Stage 10: Formatter

`duc fmt` should behave like a Dudu-aware `black`.

Format:

- indentation
- blank lines
- operator spacing
- trailing commas in multiline calls/literals
- import ordering
- final newline

Warn/error on naming for Dudu-native declarations:

- types: `PascalCase`
- constants: `ALL_CAPS`
- functions, locals, fields: `snake_case`
- imported names: preserved

## Stage 11: Tests

The test suite uses Python-shaped `.dd` fixtures.

Recommended layout:

```text
tests/
    python_subset/
        basic/
        classes/
        functions/
        loops/
        tuples/
        results/
        containers/
        pointers/
        interop_c/
        interop_cpp/
    expected_cpp/
```

Test modes:

- parse-only
- typecheck-only
- emit C++ and diff important snippets
- compile generated C++
- run generated binary
- optional external library probes

Optional probes:

- GLM
- raylib
- SQLite
- OpenCV

Skip optional probes when dependencies are missing.

Run available local probes with:

```sh
./scripts/probe_optional.sh
```

## User Workflow

Dudu should fit into normal native development instead of hiding the C/C++
toolchain.

Expected daily commands:

```sh
dudu check
dudu build
dudu run
dudu test
dudu clean
duc fmt
```

Expected project flow:

1. Write `.dd` files.
2. Configure native dependencies in `dudu.toml`.
3. Run `dudu check` for Dudu parse/type errors.
4. Run `dudu build` to emit C++ and invoke the configured native build.
5. Debug generated C++ when necessary.
6. Use generated `.hpp` files from C++ projects when Dudu code is a library.

Expected implementation flow:

1. Work in cohesive slices.
2. Run the relevant formatter and tests for the touched area.
3. Commit frequently at reasonable green checkpoints.
4. Keep each commit scoped enough to review or revert cleanly.

`duc` supports direct one-file use:

```sh
duc run examples/raylib_game.dd
duc emit examples/raylib_game.dd -o build/raylib_game.cpp
```

`dudu` handles project use:

```sh
dudu build
dudu run
dudu test
```

## Package And Build Manifest

`dudu.toml` describes the native build environment.

Sketch:

```toml
name = "game_tool"
entry = "src/main.dd"
kind = "executable"
mode = "hosted"

[cxx]
standard = "c++20"
compiler = "clang++"

[include]
paths = ["include"]

[link]
paths = ["third_party/lib"]
libs = ["raylib", "imgui"]

[cc]
defines = ["IMGUI_IMPL_OPENGL_LOADER_GLAD"]
flags = ["-Wall", "-Wextra"]
warnings_as_errors = false

[pkg]
libs = ["raylib", "sdl3"]

[build]
dir = "build"
DEBUG = true
RENDER_BACKEND = "vulkan"
```

Target modes:

```text
hosted
freestanding
embedded
cuda
shader
```

Target kinds:

```text
executable
library
shared_library
```

The build driver should preserve and expose generated files:

```text
build/generated/main.cpp
build/generated/main.hpp
build/compile_commands.json
```

Generated code is a debugging surface, not an implementation secret.

## Diagnostics

Dudu errors should point at Dudu source first, then include generated C++ context
when needed.

Error format:

```text
src/main.dd:12:17: type error: cannot assign i32 to f32 without an explicit cast
    y: f32 = x
             ^
help: write f32(x)
```

Required diagnostic categories:

- lexical errors
- indentation errors
- parse errors
- unresolved names
- type mismatch
- missing return
- rejected implicit cast
- const assignment
- all-caps naming misuse
- invalid compile-time expression
- failed `static_assert`
- missing `build.NAME` flag
- pointer/reference escape of local values
- tuple destructuring arity mismatch
- unsupported Python feature
- C/C++ import/include failure
- generated C++ compile failure
- linker failure

Generated C++ failures should be summarized:

```text
src/render.dd:44:9: C++ interop error: no matching overload for rl.DrawText
    rl.DrawText(text, x, y, size)
        ^
clang++: candidate expects const char*, got str
help: use text.c_str()
```

The full native compiler output should remain available with:

```sh
dudu build --verbose
```

## Optional Hardware And Library Tests

The validation matrix includes tests for CUDA, Vulkan, embedded, UART, OpenCV,
raylib, SDL3, FFmpeg, and other libraries that may not exist on a developer
machine.

Test policy:

- Core language tests always run.
- Local C/C++ fixture tests always run.
- Optional library probes compile/run only when dependencies are detected.
- Hardware-dependent tests can have compile-only variants.
- GPU/embedded tests can be validated by target toolchain compile without
  requiring the physical device.
- Skipped probes must print a clear reason.

Example output:

```text
PASS core/classes
PASS interop_c/sqlite
SKIP cuda_saxpy: nvcc not found
SKIP embedded_uart: arm-none-eabi-g++ not found
SKIP raylib_imgui_tool: raylib pkg-config package not found
```

Validation tests should distinguish:

```text
parse
typecheck
emit
native_compile
link
run
hardware_run
```

This lets a machine without CUDA hardware still validate that CUDA-shaped Dudu
emits and compiles with a CUDA toolchain.

## Generated C++ Policy

Generated C++ should be:

- readable
- stable enough for debugging
- formatted consistently
- split into `.hpp/.cpp` for library targets
- annotated with source comments for diagnostics
- compatible with ordinary C++ debuggers and profilers

Example generated comments:

```cpp
// src/main.dd:12
int32_t value = add(1, 2);
```

Do not rely on generated C++ being beautiful public API by itself. Public API is
the generated header plus documented Dudu declarations.

## Editor And Tooling

Required tooling commands:

```sh
duc fmt .
duc fmt --check .
dudu check
dudu build
dudu test
```

Commit workflow:

- commit frequently at reasonable green checkpoints
- keep commits focused on one feature, diagnostic, doc update, or test slice
- run formatter and tests before each commit

Editor integration should support:

- format on save
- parse/type diagnostics
- go to definition inside Dudu modules
- generated C++ location lookup
- basic completion for Dudu symbols
- imported C/C++ completion once Clang-backed import is available
- syntax highlighting
- file icons and `.dd` file association
- command palette actions for formatting, checking, building, and running

The syntax intentionally stays close to Python so existing editor tokenization
can provide a usable baseline before a full language server exists.

Editor deliverables:

```text
editors/
    vscode/
        package.json
        syntaxes/dudu.tmLanguage.json
        language-configuration.json
    vim/
        syntax/dudu.vim
        ftdetect/dudu.vim
    nvim/
        queries/dudu/highlights.scm
```

VS Code development flow:

- keep a local extension under `editors/vscode`
- run it with VS Code's Extension Development Host
- associate `.dd` files with Dudu
- start with TextMate grammar based on Python plus Dudu-specific additions:
  - `import c`
  - `import cpp`
  - fixed-width scalar types
  - `*T`, `&T`, `const[T]`
  - typed `for name: Type in`
  - `fn(...) -> T` function pointer types
  - target attributes like `@cuda.global`

Vim/Neovim flow:

- basic Vim syntax and file detection live in the repo
- Neovim Tree-sitter queries are added when a grammar exists
- users can symlink or copy the files during development

Language server:

- `duc lsp` should provide diagnostics and navigation after parser/typechecker
  APIs are stable
- syntax highlighting does not wait for LSP

## Performance Requirements

Dudu-generated code should perform equivalently to hand-written C++ for the same
data structures and operations.

Performance policy:

- Dudu abstractions must lower to predictable C++.
- No hidden interpreter.
- No required garbage collector.
- No hidden dynamic dispatch unless requested by the type being used.
- No hidden heap allocation for value construction.
- `list[T]` lowers to an owning dynamic array such as `std::vector<T>`.
- `array[T] = literal` lowers to fixed contiguous storage with inferred shape.
- `array[T][N]` lowers to fixed storage such as `std::array<T, N>`.
- `array[T][M, N]` lowers to fixed contiguous matrix/tensor storage.
- Tuple returns lower to small generated aggregates.
- Function pointer calls lower to raw function pointer calls.
- Raw pointer operations lower to raw pointer operations.

Every performance-sensitive feature needs a generated C++ inspection test and a
runtime benchmark.

Benchmark categories:

1. scalar arithmetic loops
2. function calls and inlining
3. struct construction and field access
4. fixed arrays
5. `list[T]` iteration and append
6. `dict[K, V]` lookup and insertion
7. tuple return/destructuring
8. function pointer calls
9. raw pointer traversal
10. image processing loop
11. audio buffer fill loop
12. matrix/vector math with GLM
13. C callback overhead
14. C++ method call overhead
15. generated library called from C++

Each benchmark should compare:

```text
Dudu source -> generated C++ -> optimized native binary
hand-written C++ equivalent -> optimized native binary
```

Acceptance rule:

```text
Generated Dudu code must match the hand-written C++ within measurement noise
for equivalent generated operations.
```

If a feature cannot meet that bar, the generated C++ must make the cost visible
and the docs must explain the cost.

Benchmark commands:

```sh
duc bench
duc bench --compare-cpp
duc bench --emit-report build/benchmarks/report.json
./scripts/bench.sh 10000000 --max-ratio 1.10
```

Benchmark reports should include:

- compiler version
- C++ compiler and flags
- CPU/GPU info when relevant
- mean/median/p95 timings
- generated C++ path
- hand-written C++ comparison path
- Dudu/C++ timing ratios
- binary size where relevant

Performance regressions should fail CI for core benchmarks once the benchmark
suite exists.

## Compile-Time Evaluation

Compile-time facilities:

```python
WIDTH: i32 = 320
HEIGHT: i32 = 240
PIXELS: i32 = WIDTH * HEIGHT

static_assert(PIXELS == 76800)

@constexpr
def align_up(value: usize, align: usize) -> usize:
    return (value + align - 1) & ~(align - 1)


BUFFER_SIZE: usize = align_up(1500, 64)
```

Build configuration uses the `build` namespace:

```python
if build.DEBUG:
    enable_validation_layers()

if build.RENDER_BACKEND == "vulkan":
    init_vulkan()
elif build.RENDER_BACKEND == "raylib":
    init_raylib()
```

Sources for `build.NAME` values:

```toml
[build]
DEBUG = true
RENDER_BACKEND = "vulkan"
```

```sh
dudu build -DDEBUG=true -DRENDER_BACKEND=vulkan
```

Rules:

- `build.NAME` values are compile-time known.
- branches depending only on `build.NAME` are compile-time selected.
- all-caps constants can initialize array sizes and other compile-time contexts.
- `@constexpr` functions are valid in compile-time contexts.
- `static_assert(expr)` evaluates at compile time.
- generated C++ should use `constexpr` where the Dudu expression is
  compile-time evaluable.

## Distribution

Dudu should be easy to install without asking users to build the compiler by
hand.

Release artifacts:

- Linux x86_64 tarball
- Linux aarch64 tarball
- macOS universal or per-arch tarball
- Windows x86_64 zip
- source tarball
- checksums
- signatures when release signing exists

Package channels:

- GitHub Releases
- Ubuntu/Debian `.deb`
- Fedora/RHEL `.rpm`
- Homebrew tap for macOS and Linux
- Arch AUR package
- Windows winget package
- Scoop package
- Docker image for CI

The compiler should also remain buildable from source:

```sh
cmake --preset dev
cmake --build build
```

Distribution validation:

- fresh install can run `duc --version`
- fresh install can run `duc check`
- fresh install can build a hello project
- package includes runtime/prelude headers
- package includes CMake/toolchain integration files
- package includes shell completions
- package includes license and docs

Versioning:

```text
duc --version
dudu language version
runtime/prelude version
generated C++ schema version
```

Packages should not bundle large third-party SDKs such as CUDA, Vulkan, SDL,
raylib, or platform SDKs. They should detect and use system/toolchain installs.

## Real-World Validation Matrix

These are the concrete stress tests Dudu should grow toward. Each test should be
a real `.dd` program that emits C++, builds with normal C/C++ tooling, and runs
or compiles against the actual target library.

### Core Language

1. **hello_native**
   - typed locals, `main`, return values, string output
   - generated C++ compile/run

2. **classes_and_methods**
   - `class`, fields, methods, `self`, constructors
   - generated `.hpp/.cpp`

3. **tuple_return_destructure**
   - `tuple[...]` return
   - generated aggregate result type
   - C++ structured binding in emitted code

4. **result_option**
   - `Result[T, E]`
   - `Option[T]`
   - explicit error handling

5. **explicit_casts**
   - fixed-width integer casts
   - float casts
   - rejected implicit narrowing/widening in Dudu-native code

6. **fixed_arrays**
   - `array[T][N]`
   - shaped fixed arrays such as `array[f32][4, 4]`
   - indexing and passing by reference

7. **function_pointers**
   - `fn(A, B) -> R`
   - comparator callback
   - void callback
   - named `def` function values coercing to raw function pointers when they do
     not capture

8. **value_vs_pointer_containers**
   - `list[Thing]` owns values
   - `list[*Thing]` is non-owning
   - reject obvious escaped pointer to local

### C Interop

9. **stdio_math**
   - `stdio.h`
   - `math.h`
   - primitive args/returns

10. **sqlite_crud**
   - `sqlite3.h`
   - output pointer parameters
   - C constants
   - C callbacks if practical
   - `Result[T, E]` wrapping

11. **posix_mmap_hash**
   - `fcntl.h`
   - `sys/mman.h`
   - `sys/stat.h`
   - raw pointers
   - manual cleanup

12. **c_callback_sort_or_visit**
   - C function pointer callbacks
   - user data pointer if API supports it

13. **c_struct_layout**
   - imported C structs
   - field access
   - pass by pointer and by value

### C++ Interop

14. **glm_math**
   - header-only C++ library
   - imported namespace alias
   - vector/matrix types
   - operator overloads
   - functions like `glm.dot`

15. **std_vector_map_string**
   - imported C++ templates
   - `std.vector[T]`
   - `std.unordered_map[K, V]`
   - `std.string` interop with Dudu `str`

16. **unique_ptr_move_only**
   - `std.unique_ptr[T]`
   - move-only imported type
   - generated C++ compile validation
   - explicit `std.move` only where required

17. **filesystem_paths**
   - `std.filesystem.path`
   - overloaded constructors
   - namespace lowering

18. **chrono_timers**
   - nested namespaces
   - templates/durations
   - overloaded arithmetic

19. **eigen_matrix**
   - heavy C++ templates
   - non-type template args
   - operator overloads
   - compile-time errors surfaced clearly

20. **opencv_image_filter**
   - C++ classes
   - method calls
   - template member calls such as `mat.at[T](...)`
   - large external dependency

### Graphics And UI

21. **raylib_game**
   - real raylib window
   - input polling
   - drawing
   - C-style structs
   - resource load/unload

22. **raylib_imgui_tool**
   - raylib
   - Dear ImGui or rlImGui integration
   - immediate-mode GUI calls
   - frame loop

23. **sdl3_window**
   - SDL3 init/window/renderer
   - event loop
   - C callbacks or event structs

24. **sdl3_imgui_tool**
   - SDL3
   - Dear ImGui backend
   - C++ backend functions
   - UI state structs

25. **glfw_opengl_triangle**
   - GLFW
   - OpenGL function loading
   - C function pointer proc lookup
   - raw handles

26. **vulkan_triangle**
   - Vulkan headers
   - large C API
   - structs with `sType`
   - arrays of structs
   - pointer chains
   - explicit cleanup

27. **vulkan_imgui_overlay**
   - Vulkan
   - Dear ImGui Vulkan backend
   - descriptor pools
   - command buffers
   - stress cleanup/order rules

28. **wgpu_native_compute**
   - C API with handles
   - callbacks
   - async-ish polling model
   - shader module input

### Audio, Video, And Media

29. **raylib_audio_synth**
   - realtime-ish audio stream update
   - fixed arrays
   - raw sample buffers

30. **miniaudio_callback**
   - C callback API
   - raw audio buffer pointer
   - user data pointer

31. **ffmpeg_probe_decode**
   - FFmpeg C libraries
   - pointer-heavy APIs
   - explicit cleanup
   - nested structs

32. **stb_image_loader**
   - single-header C library
   - macros handled by wrapper header
   - malloc/free ownership

### Systems And Platform

33. **posix_threads_mutex**
   - pthreads or C++ threads
   - mutex/lock wrappers
   - function pointer thread entry if using pthreads

34. **std_thread_atomic_queue**
   - `std.thread`
   - `atomic[T]`
   - references to shared state

35. **memory_mapped_registers**
   - `volatile[T]`
   - fixed addresses
   - packed register structs

36. **binary_packet_parser**
   - `@packed`
   - `sizeof`
   - `offsetof`
   - endian conversion helpers

37. **arena_allocator**
   - library-defined arena API
   - `arena.alloc`
   - `arena.reset`
   - Dudu imposes no arena semantics

38. **plugin_dynamic_library**
   - generate `.hpp/.cpp`
   - build shared library
   - C++ host loads/calls Dudu-generated API

39. **cpp_consumes_dudu_library**
   - Dudu classes/functions in generated header
   - C++ test includes header and calls functions
   - validates importability from C++

### CUDA, GPU, And Shaders

40. **cuda_saxpy**
   - `@cuda.global`
   - device pointers
   - cudaMalloc/cudaMemcpy/cudaFree
   - launch helper

41. **cuda_shared_memory_tile**
   - shared address space
   - block/thread IDs
   - device-only restrictions

42. **opencl_kernel_host**
   - OpenCL C API
   - kernel source loading
   - buffers
   - callbacks/errors

43. **shader_compute_blur**
   - `@shader.compute`
   - storage address space
   - workgroup size
   - backend-specific emission probe

44. **spirv_or_wgsl_codegen_probe**
   - shader subset parse/typecheck
   - emit shader target or wrapped C++ host code
   - verifies address spaces and entry attributes

### Embedded And Freestanding

45. **freestanding_no_std**
   - no C++ stdlib containers
   - fixed arrays only
   - no heap
   - no exceptions/RTTI flags

46. **embedded_uart**
   - volatile register struct
   - fixed memory address
   - no runtime init requirement

47. **linker_script_probe**
   - target manifest with linker script
   - custom section attributes

48. **interrupt_handler**
   - target attribute for ISR
   - C ABI name
   - volatile/atomic interaction

### Build And Tooling

49. **cmake_package**
   - Dudu target inside a CMake project
   - generated sources in build dir
   - compile_commands.json preserved

50. **pkg_config_raylib**
   - dependency discovered through pkg-config
   - skipped cleanly when missing

51. **multi_file_project**
   - Dudu module imports
   - generated header boundaries
   - circular import diagnostics

52. **formatter_check**
   - `duc fmt --check`
   - canonical formatting for examples

53. **diagnostic_source_ranges**
   - type error source spans
   - parse error source spans
   - imported C++ compile error attribution

54. **large_cpp_error_surface**
   - intentional bad C++ interop call
   - generated C++ error summarized into Dudu source context

## Stage 12: Systems Capability

Systems examples live in `examples/` and drive the long-range capability set:

- `layout_hardware.dd`: packed/aligned layout, volatile registers, `sizeof`,
  `alignof`, and `offsetof`
- `threading_atomics.dd`: atomics, threads, and reference parameters
- `cuda_kernel.dd`: CUDA attributes and device address spaces
- `shader_compute.dd`: shader-style compute attributes and storage address
  spaces
- `native_escape.dd`: inline C++ escape hatches
- `modules_visibility.dd`: generated header surface with underscore-private helpers

The core gate parses and emits these examples. Optional probes compile and run
library-backed fixtures when local dependencies are installed.

## Risks

- Python syntax is large, while Dudu deliberately supports only the static
  systems subset.
- Full C++ interop needs Clang tooling.
- `*T` and `&T` are not Python syntax, so parser reuse is limited there.
- Dot lowering between namespace/member/pointer access requires type knowledge.
- Good diagnostics matter because users will expect Python-like clarity.
