# Dudu Appearance Spec

This is the current surface-language direction for Dudu.

Dudu should look like Python, but compile to straightforward C++.

The goal is no longer to invent a quirky new surface syntax. The goal is:

```text
Python syntax people already know.
Static C/C++-style types.
Readable generated C++.
Direct C and C++ interop.
```

## Files

Dudu source files use `.dd`.

```text
main.dd
math.dd
renderer.dd
```

The syntax stays close enough to Python that editor tooling can reuse Python
grammar pieces where practical.

## Comments

Comments are Python comments.

```python
# hello
```

## Naming

Dudu-native code follows a common C++-friendly style:

```text
Types:      PascalCase
Constants:  ALL_CAPS
Everything else: snake_case
```

```python
class PlayerState:
    current_health: i32
    max_health: i32


def update_player(player: PlayerState, delta_time: f32):
    player.current_health -= 1
```

Imported C/C++ names preserve source spelling:

```python
rl.InitWindow(800, 450, "Dudu")
rl.DrawText("hello", 20, 20, 20, rl.WHITE)
GL_TEXTURE_2D
```

For Dudu-native declarations, uppercase spelling is semantic. An `ALL_CAPS`
binding is a constant and cannot be reassigned. Non-constant bindings must not
use all-caps names.

## Imports

Dudu modules use Python-shaped imports and keep names qualified by default:

```python
import math
import renderer.camera as camera
from renderer.camera import Camera
```

`import renderer.camera` binds the top-level `renderer` name. Code accesses
members through the qualified module path:

```python
cam = renderer.camera.Camera()
```

`import renderer.camera as camera` binds only `camera`:

```python
cam = camera.Camera()
```

`from renderer.camera import Camera` binds `Camera` directly in the current
module. If two direct imports create the same name, compilation fails unless
one or both imports use aliases:

```python
from ui.button import Button as UiButton
from game.input import Button as InputButton
```

Reexports are ordinary Dudu modules. A package can provide a facade module:

```python
# game.dd
from game.player import Player
from game.world import World
```

Users can then write:

```python
import game

p = game.Player()
```

Packages do not need facade modules. Qualified imports are the default answer
to name collisions.

C and C++ headers need explicit foreign imports. These are not Python syntax,
but they should stay visually close:

```python
import c "stdio.h" as c
import cpp "raylib.h" as rl
import cpp "glm/glm.hpp" as glm
```

The quoted path is a header spelling, not a Dudu module name.

Dudu source files are the source of truth. The compiler emits generated
`.hpp`/`.cpp` files for C++ consumers and generated `.h` files for C ABI
exports. Dudu interface files are not required for normal use.

## Blocks

Blocks are Python blocks:

```python
if alive:
    update()
    draw()
else:
    reset()
```

Colons are back. This makes the language immediately legible as Python-shaped
code and makes editor tooling easier.

## Assignment

Assignment is Python assignment:

```python
x = 10
name = "dudu"
```

Equality:

```python
if x == 10:
    print("ten")
```

Compound assignment:

```python
count += 1
count -= 1
total *= scale
total /= scale
```

## Built-In Types

Scalar types keep Rust/C-style fixed-width names:

```python
bool
i8
i16
i32
i64
u8
u16
u32
u64
isize
usize
f32
f64
void
str
cstr
```

Dudu source uses these spellings directly. It does not accept `int`, `float`,
or `double` as core-type aliases, because those names are ambiguous across C and
C++ ABIs. Imported C/C++ headers are still normalized into Dudu's fixed-width
type model for checking and editor tooling.

Likely C++ lowerings:

```text
str  -> std::string
cstr -> const char*
```

`void` exists for C/C++ interop. Normal Python-style functions omit `-> void`.

## Variables

Typed locals use Python annotations:

```python
count: i32 = 0
name: str = "dudu"
player: PlayerState
```

Inference is allowed when the initializer is enough:

```python
count = 10
scale = 2.0
player = make_player()
```

Constants use mandatory `ALL_CAPS` names.

```python
MAX_PLAYERS: i32 = 64
PI: f32 = 3.14159265
TITLE = "Dudu"
```

An `ALL_CAPS` binding cannot be reassigned. At module scope, compile-time
constant initializers can lower to C++ `constexpr`; other constant initializers
can lower to `const`.

Const-qualified C++ types use `const`:

```python
p: *const[i32]
r: &const[PlayerState]
```

This keeps the low-level C/C++ concepts visible without copying C++'s
declaration grammar.

## Functions

Functions are Python functions with required types at boundaries.

```python
def add(a: i32, b: i32) -> i32:
    return a + b
```

No return type means no returned value:

```python
def log(msg: cstr):
    c.printf("msg: %s\n", msg)
```

`return` is normal Python `return`.

```python
def clamp(x: i32, lo: i32, hi: i32) -> i32:
    if x < lo:
        return lo
    if x > hi:
        return hi
    return x
```

Expression-last implicit return is gone. If this is Python-shaped, use
`return`.

## Tests

Tests use explicit Python-style decorators.

```python
@test
def add_works():
    assert add(20, 22) == 42

@test
def returns_bool() -> bool:
    return add(1, 2) == 3

@test
def returns_code() -> i32:
    return add(2, 2) - 4
```

`@test` functions are free functions, take no args, and return `void`, `bool`,
or `i32`. `void` passes unless an assertion fails, `bool` passes on `True`,
and `i32` passes on zero.

```sh
dudu test
dudu test src/math.dd
dudu test add_works
dudu test --filter add
```

## Calls

Calls use normal Python call syntax:

```python
draw_circle(x, y, radius, color)
```

Multiline calls are Python-style:

```python
draw_circle(
    x,
    y,
    radius,
    color,
)
```

Nested calls are normal Python:

```python
draw_circle(
    add(x, 10),
    y,
    radius,
    rgb(1, 0, 0),
)
```

Commas are back. Parentheses are back. This makes the language much more
familiar and far easier to parse/tool.

## Classes

`class` declares a C++ struct-like type by default.

```python
class Vec2:
    x: f32
    y: f32
```

Methods are Python-style:

```python
class PlayerState:
    pos: Vec2
    hp: i32

    def damage(self, amount: i32):
        self.hp -= amount

    def alive(self) -> bool:
        return self.hp > 0
```

Out-of-line methods can be allowed for C++-style organization:

```python
def PlayerState.damage(self, amount: i32):
    self.hp -= amount
```

`self` is explicit. That is Python, and it avoids hidden receiver rules.

## Enums

Enums can stay simple.

```python
enum Direction:
    North
    South
    East
    West
```

With backing type:

```python
enum Color: u8
    Red = 1
    Green = 2
    Blue = 3
```

Enum variants are `PascalCase`. A plain integer enum is the zero-payload form of
the same enum model used by payload variants.

## Type Aliases

Use Python-style assignment for aliases.

```python
type PlayerId = u64
type ScoreMap = dict[str, i32]
```

Native typedefs from imported C/C++ headers are discovered automatically when
Clang can scan the imported header:

```python
import c "SDL3/SDL.h" as sdl

def pump_events() -> bool:
    event: SDL_Event
    while sdl.SDL_PollEvent(&event):
        if event.type == sdl.SDL_EVENT_QUIT:
            return False
    return True
```

Manual `type Name` declarations remain available as an escape hatch for unusual
generated or platform headers, but normal C/C++ interop should use imported
headers directly.

An uninitialized local value such as `event: SDL_Event` lowers to C++ value
initialization, `SDL_Event event{};`. `None` remains for null pointers and
optional values.

This should lower to C++ `using`.

## Multiple Return Values

Python-style tuple return and destructuring are allowed.

```python
def divmod_i32(a: i32, b: i32) -> tuple[i32, i32]:
    return a / b, a % b


q, r = divmod_i32(10, 3)
```

The generated C++ does not have to use `std::tuple`. The preferred lowering is
a small generated aggregate so C++ callers can inspect it easily:

```cpp
struct divmod_i32_result {
    int32_t _0;
    int32_t _1;
};
```

C++ callers can still use structured bindings:

```cpp
auto [q, r] = divmod_i32(10, 3);
```

For public APIs where names matter, return a class.

```python
class MinMax:
    lo: i32
    hi: i32


def minmax(values: list[i32]) -> MinMax:
    lo: i32 = values[0]
    hi: i32 = values[0]

    for v: i32 in values:
        if v < lo:
            lo = v
        if v > hi:
            hi = v

    return MinMax(lo, hi)
```

Tuple return lowers to a generated aggregate by default. `std::tuple` is an
imported C++ library type, not the default tuple-return lowering.

## Results And Errors

Dudu uses `Result` and `Option` for normal error handling.

Core prelude types:

```python
Option[T]
Result[T, E]
```

C++ exception interop is available when native libraries require it:

```python
import cpp "stdexcept"

def load() -> i32:
    try:
        raise std.runtime_error("boom")
    except e: std.exception:
        return 42
    except:
        return 1
```

`try`, `except`, and `raise` lower to C++ `try`, `catch`, and `throw`.
`finally` is not part of Dudu.

Example:

```python
enum ReadError:
    not_found
    permission_denied
    invalid_utf8


def read_text(path: str) -> Result[str, ReadError]:
    if not fs.exists(path):
        return Err(ReadError.NotFound)

    return Ok(fs.read_all(path))
```

Use explicit handling:

```python
result = read_text("config.txt")

if result.ok:
    text = result.value
else:
    log_error(result.err)
```

Likely lowerings:

```text
Option[T]    -> std::optional<T>
Result[T, E] -> dudu::Result<T, E>
```

## Containers

Use Python typing spellings.

```python
values: list[i32]
tiles: array[Tile][256]
matrix: array[f32][4, 4]
palette: array[Color] = [Color.RED, Color.GREEN, Color.BLUE]
scores: dict[str, i32]
visited: set[TileId]
cache: dict[str, dict[i32, set[f32]]]
```

Likely C++ lowerings:

```text
list[T]      -> std::vector<T>
array[T] = literal -> fixed contiguous storage with inferred shape
array[T][N]  -> fixed contiguous storage
array[T][M, N] -> fixed contiguous matrix/tensor storage
dict[K, V]   -> std::unordered_map<K, V>
set[T]       -> std::unordered_set<T>
tuple[...]   -> dudu::TupleN<...> aggregate
```

`array[T]` without an initializer does not specify enough storage. Use
`array[T][shape]` when there is no RHS:

```python
scratch: array[f32][4, 4]
identity: array[f32] = [
    [1.0, 0.0, 0.0, 0.0],
    [0.0, 1.0, 0.0, 0.0],
    [0.0, 0.0, 1.0, 0.0],
    [0.0, 0.0, 0.0, 1.0],
]
```

Ragged literals are errors, and empty literals need an explicit shape.

Indexing is Python indexing:

```python
x = values[3]
cell = grid[y, x]
values[3] = 10
scores["bob"] = 42
```

Multidimensional fixed arrays and tensor-like values should support comma
indexing:

```python
mat: array[f32][4, 4]
mat[row, col] = 1.0

image: array[Color][height, width]
pixel = image[y, x]
```

Slicing should be supported for numeric and graphics code, but copy-vs-view
semantics must be explicit. The preferred model is that slices produce views
where possible, and callers explicitly copy when they want ownership:

```python
row = mat[row_index, :]
patch = image[y0:y1, x0:x1]
copy = list(row)
```

The detailed indexing and slicing design is tracked in
[Arrays, Matrix, Tensor, And Slicing Plan](arrays-indexing-plan.md).

Vector-like types can support GLSL-style swizzling when the type opts into
known components:

```python
v: Vec4[f32]
xy = v.xy
rgb = color.rgb
v.xy = Vec2[f32](1.0, 2.0)
```

Swizzling is compile-time syntax for vector-like types, not dynamic member
lookup. Repeated components are allowed on reads such as `v.xx`, but not writes.

## Literals

List literals:

```python
values: list[i32] = [1, 2, 3]
```

Dict literals:

```python
scores: dict[str, i32] = {
    "bob": 42,
    "ada": 99,
}
```

Set literals:

```python
seen: set[str] = {"bob", "ada"}
```

These should look like Python.

## C++ Templates

C++ template arguments use Python-style square brackets too:

```python
items: std.vector[i32]
table: std.unordered_map[str, PlayerState]
```

Use imported aliases when C++ already provides them:

```python
a: glm.vec3
b: glm.vec3
d: f32 = glm.dot(a, b)
```

Template function calls use bracketed template args:

```python
ptr = std.make_unique[PlayerState](10, 20)
```

This is not Python, but it is readable and maps directly to C++ templates.

## Pointers And References

Pointers and references use familiar C-family symbols in type annotations.

```python
p: *i32
r: &i32
pc: *const[i32]
rc: &const[PlayerState]
```

Address-of and dereference use C-family operators:

```python
value: i32 = 42
p: *i32 = &value

copy: i32 = *p
*p = 99
```

References bind like C++ references:

```python
value: i32 = 42
r: &i32 = value
r += 1
```

This is intentionally boring for C/C++ users, but still avoids the classic C
trap where `int* a, b` declares only `a` as a pointer. In Dudu, the modifier is
part of the type:

```python
a: *i32
b: i32
```

## Function Pointer Types

Raw function pointers use `fn`.

```python
cmp: fn(i32, i32) -> bool
audio_cb: fn(*f32, i32)
```

If the return type is omitted, the function pointer returns nothing.

```python
def less(a: i32, b: i32) -> bool:
    return a < b


def sort_values(values: list[i32], cmp: fn(i32, i32) -> bool):
    native_sort(values, cmp)


sort_values(values, less)
```

Rules:

- `fn(...) -> T` is a raw function pointer type.
- `fn(...)` returns `void`.
- `def name(...)` is a statement-only declaration.
- function names are values after declaration.
- `def` expressions and anonymous function values are not part of Dudu.
- non-capturing function values can coerce to `fn(...) -> T`.
- capturing function values require a closure-capable target type such as an
  imported `std.function[...]` wrapper; they cannot coerce to raw `fn`.
- owning/capturing callable wrappers use imported C++ types such as
  `std.function[...]`.

Named function declarations replace Python `lambda`:

```python
def add_one(value: i32) -> i32:
    return value + 1

callback: fn(i32) -> i32 = add_one
callback(41)  # valid
add_one(41)  # valid
```

Callback tables stay statement-oriented:

```python
def on_spawn(event: Event):
    spawn_enemy(event.id)


def on_hit(event: Event):
    damage_player(event.id, event.damage)


handlers: map[str, fn(Event)] = {
    "spawn": on_spawn,
    "hit": on_hit,
}
```

## Allocation

Dudu does not impose an ownership or allocator model.

Value construction is the default:

```python
player = Player(100, "bob")
```

Prelude heap helpers:

```python
p: *Player = new[Player](100, "bob")
delete(p)
```

Raw C-style allocation helpers:

```python
bytes: *u8 = malloc[u8](1024)
free(bytes)
```

Rules:

- `T(...)` constructs a value.
- `new[T](args...)` allocates and constructs one `T`, returning `*T`.
- `delete(p)` destroys and deallocates memory created by `new`.
- `malloc[T](count)` allocates raw storage for `count` `T` values.
- `free(p)` frees memory created by `malloc`.
- custom allocators are ordinary library APIs, not language rules.
- fixed-size arrays use `array[T][N]` or `array[T][M, N]`.
- dynamic owning arrays use `list[T]`.

Specialized allocation belongs to libraries and arenas:

```python
players = arena.list[Player](count)
scratch = frame_allocator.list[f32](1024)
```

Those APIs should return normal library-owned values with RAII/destructor
cleanup where possible. Raw pointers remain available for low-level interop, but
they are not the main array story.

Value containers own values:

```python
def make_players() -> list[Player]:
    players: list[Player] = []
    players.append(Player(100, "bob"))
    players.append(Player(80, "ada"))
    return players
```

This is safe. `list[Player]` lowers to an owning dynamic array, likely
`std::vector<Player>`. Appending `Player(...)` stores a value in the list, and
returning the list returns the owning container.

Pointer containers do not own pointees:

```python
players: list[*Player] = []
```

The programmer or library must ensure pointed-to objects live long enough. The
compiler should reject obvious escapes like returning `&local_value` through a
pointer container.

## Scope Cleanup

Dudu should prefer RAII and value ownership over a `defer` statement.

Use values whose destructors close resources:

```python
file = File.open(path)
data = file.read_all()
```

Use explicit scope blocks when lifetime needs to be visually constrained:

```python
with_scope:
    lock = LockGuard(mutex)
    update_shared_state()
```

The exact scope-block spelling can change, but the core rule is that cleanup is
owned by destructors and lexical scope. Dudu should not add Go/Zig-style
`defer` as the primary cleanup model.

Arena example:

```python
arena = Arena(1024 * 1024)

enemy: *Enemy = arena.make[Enemy](spawn)
scratch: *u8 = arena.alloc[u8](4096)

arena.reset()
```

Dudu does not define what `arena.make`, `arena.alloc`, or `arena.reset` mean.
Those are just method calls supplied by the arena library.

## Compile-Time Values

All-caps constants can participate in compile-time expressions when their
initializer is compile-time evaluable.

```python
WIDTH: i32 = 320
HEIGHT: i32 = 240
PIXELS: i32 = WIDTH * HEIGHT

pixels: Color[PIXELS]
```

Build flags live in the `build` namespace.

```python
if build.DEBUG:
    enable_validation_layers()

if build.RENDER_BACKEND == "vulkan":
    init_vulkan()
elif build.RENDER_BACKEND == "raylib":
    init_raylib()
```

Build flags come from `dudu.toml` or command-line `-D` values:

```toml
[build]
DEBUG = true
RENDER_BACKEND = "vulkan"
```

```sh
dudu build -DDEBUG=true -DRENDER_BACKEND=vulkan
```

Branches depending only on `build.*` values are compile-time selected.

Static assertions use `static_assert`.

```python
static_assert(sizeof[PacketHeader]() == 12)
static_assert(alignof[Mat4Block]() == 16)
```

Compile-time functions use `@constexpr`.

```python
@constexpr
def align_up(value: usize, align: usize) -> usize:
    return (value + align - 1) & ~(align - 1)


BUFFER_SIZE: usize = align_up(1500, 64)
```

`@constexpr` maps to generated C++ `constexpr` where possible. It is also the
Dudu signal that a function can be evaluated for constants, static assertions,
array sizes, and build-time branches.

## Member And Namespace Access

Use dot, like Python.

```python
player.pos.x
std.vector
glm.vec3
rl.InitWindow
```

The emitter lowers based on context:

```text
object.field   -> object.field
pointer.field  -> pointer->field
namespace.name -> namespace::name
```

## Conditionals

Conditionals are Python conditionals.

```python
if hp <= 0:
    dead = true
else:
    hp -= 1
```

Conditional expressions are not part of the core language. Use an explicit
local and ordinary control flow instead:

```python
status: str
if hp <= 0:
    status = "dead"
else:
    status = "alive"
```

This keeps control flow statement-oriented and avoids expression-only special
cases that do not materially improve systems code.

## Rejected Python Sugar

Dudu intentionally leaves out some compact Python expression forms:

- `lambda`
- list comprehensions
- dict comprehensions
- set comprehensions
- generator expressions
- ternary conditional expressions
- `with` context-manager statements

These forms hide control flow, allocation, capture, or lifetime behavior inside
expressions. Prefer named `def` declarations and explicit loops:

```python
def on_spawn(event: Event):
    spawn_enemy(event.id)

handlers["spawn"] = on_spawn

squares: list[i32] = []
for value: i32 in values:
    squares.append(value * value)
```

## Async And Concurrency

Dudu should not copy Python's `async`/`await` model into the core language.
C++ coroutine support exists, but it is library- and ABI-shaped in ways that do
not map cleanly to Python async syntax.

Concurrency should start with ordinary native tools:

- threads
- atomics
- mutexes and lock guards
- event loops from imported C/C++ libraries
- callbacks
- nonblocking I/O APIs
- C++ coroutine/task libraries through normal interop

This is not a death sentence for networking code. C and C++ networking is often
written with event loops, callbacks, worker threads, or library-provided task
types. Dudu should consume those APIs well before designing its own coroutine
runtime.

## Loops

While:

```python
while i < count:
    sum += values[i]
    i += 1
```

Ranges:

```python
for i: i32 in range(count):
    sum += values[i]
```

Collections:

```python
for item in items:
    print(item)
```

Loop variables can be explicitly typed:

```python
for player: &Player in players:
    player.hp -= 1

for player: &const[Player] in players:
    draw_player(player)
```

This is a deliberate Dudu extension to Python. It gives C++ programmers control
over copy, mutable reference, and const reference behavior without predeclaring
loop variables.

`range` should lower to a C++ integer loop.

Labeled loops are allowed for nested loop control. Labels attach only to
`for`/`while`, not arbitrary statements, and can only be targeted by `break` or
`continue`.

```python
outer: for y: i32 in range(height):
    for x: i32 in range(width):
        if blocked(x, y):
            break outer
        if skip_column(x):
            continue outer
        visit(x, y)
```

This covers the useful Rust-style nested-loop escape without adding general
`goto`.

## Conversions

Dudu should avoid implicit casts.

Allowed:

- exact same type assignment
- safe construction where the target type is explicit
- explicit casts using type-call syntax

Examples:

```python
x: i32 = 10
y: i64 = i64(x)
z: f32 = f32(y)
byte: u8 = u8(x)
```

Rejected:

```python
x: i32 = 10
y: i64 = x      # explicit i64(x) required
z: f32 = x      # explicit f32(x) required
```

The goal is to keep generated C++ predictable and avoid C/C++'s surprising
implicit conversion behavior. Imported C++ APIs may still perform conversions
that are part of overload resolution, but Dudu-native assignment and return
types should require explicit casts when the type changes.

## Constructors

Construction looks like Python calls.

```python
pos: Vec2 = Vec2(10, 20)
```

Named field construction:

```python
player: PlayerState = PlayerState(
    pos=Vec2(10, 20),
    hp=100,
)
```

This should lower to aggregate initialization or constructor calls depending on
the generated C++ type.

## Class Constants, Static Fields, And Class-Scoped Functions

Class body annotations are instance fields by default, including defaults:

```python
class Player:
    hp: i32
    speed: f32 = 240.0
```

Class-scoped `ALL_CAPS` bindings are static constants and use type-qualified
access:

```python
class Color:
    r: f32
    g: f32
    b: f32
    a: f32

    WHITE: Color = Color(1.0, 1.0, 1.0, 1.0)
    MAX_CHANNEL: i32 = 255


white: Color = Color.WHITE
limit: i32 = Color.MAX_CHANNEL
```

Mutable class-shared state must use `static[T]`:

```python
class Counter:
    count: static[i32] = 0

    def bump() -> i32:
        Counter.count += 1
        return Counter.count
```

Static fields must be accessed through the type name. `self.count` always means
an instance field named `count`; it does not fall back to `Counter.count`.

Functions inside classes that do not take `self` are class-scoped functions and
lower to C++ static member functions:

```python
class Color:
    r: f32
    g: f32
    b: f32
    a: f32

    def gray(value: f32) -> Color:
        return Color(value, value, value, 1.0)
```

Class-scoped functions do not take `self`. Type-qualified access lowers to C++
`::`, while instance field and method access keeps normal member access.

## Operator Methods

Dudu-native operator overloads use `@operator(...)` and lower to C++ operators:

```python
class Vec2:
    x: i32
    y: i32

    @operator("+")
    def add(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)

    @operator("==")
    def equals(self, other: Vec2) -> bool:
        return self.x == other.x and self.y == other.y
```

Supported binary operators are `+`, `-`, `*`, `/`, and `%`. Supported comparison
operators are `==`, `!=`, `<`, `<=`, `>`, and `>=`. Operator methods take
`self` plus one argument. Comparison operators must return `bool`.

## C Interop

```python
import c "stdio.h" as c


def main() -> i32:
    c.printf("hello %s\n", "dudu")
    return 0
```

C structs, enums, functions, constants, and macros come through generated C++
includes first. The C++ compiler validates final call shapes until Dudu has a
real header importer.

## C++ Interop

```python
import cpp "raylib.h" as rl


def main() -> i32:
    rl.InitWindow(800, 450, "Dudu")

    while not rl.WindowShouldClose():
        rl.BeginDrawing()
        rl.ClearBackground(rl.BLACK)
        rl.DrawText("hello", 20, 20, 20, rl.WHITE)
        rl.EndDrawing()

    rl.CloseWindow()
    return 0
```

GLM:

```python
import cpp "glm/glm.hpp" as glm


def main() -> i32:
    a: glm.vec3
    b: glm.vec3

    d: f32 = glm.dot(a, b)
    return 0
```

Operator overloads from imported C++ types should work where generated C++
supports them.

## C++ Importability

Dudu code should generate normal `.hpp` and `.cpp` files. Top-level Dudu
functions and classes should be usable from C++ without extra export syntax.
Functions marked `@extern_c` are also emitted in generated C headers for C
callers.

```python
class Camera:
    pos: Vec3
    yaw: f32
    pitch: f32


def camera_forward(camera: Camera) -> Vec3:
    ...
```

Should produce normal C++ declarations:

```cpp
struct Camera {
    Vec3 pos;
    float yaw;
    float pitch;
};

Vec3 camera_forward(Camera camera);
```

Explicit export controls are reserved for C ABI, shared library visibility, and
name-mangling policy. They are not required for ordinary C++ use.

## Removed Python Features

Dudu is Python-shaped, not full Python. These are rejected:

- Python `finally`
- `eval` and `exec`
- monkeypatching classes, modules, or functions
- dynamic attribute creation on typed classes
- metaclasses and descriptors
- runtime `getattr`/`setattr` by arbitrary string
- changing a local variable to a different type after first assignment
- arbitrary CPython package imports
- generators and `yield`
- `async` and `await`
- multiple inheritance
- dynamic class creation
- decorators, except compiler-recognized attributes
- heterogeneous containers unless explicitly typed as a dynamic object type
- arbitrary precision Python `int` as the default integer model

The point is to preserve Python's readable syntax while choosing static,
predictable C++ semantics.

## Formatter

Dudu should have one canonical format. `dudu fmt` should behave like `black`
for Dudu projects, and `duc fmt` should provide the same formatter for direct
file-oriented compiler use.

It owns:

- indentation
- line wrapping
- import ordering
- spacing around operators
- trailing commas in multiline calls/literals
- naming diagnostics for Dudu-native declarations
- trailing whitespace and final newline

Expected commands:

```sh
dudu fmt
dudu fmt --check
duc fmt src/**/*.dd
duc fmt --check .
```

Editors should run the canonical formatter on save. Formatting should be stable
and non-configurable except for compatibility switches that are forced by a
future language version.

## Type Inference

Dudu should infer local variable types from initializers when the type is clear:

```python
count = 0
name = "dudu"
player = Player(hp=100, x=1.0, y=2.0)
dt = rl.GetFrameTime()
```

Inference is local and compile-time. It should not turn Dudu into dynamic
Python. A binding gets one static type, and assigning a different type later is
an error.

Empty container literals can use Rust-style local constraints:

```python
players = []
players.append(Player(hp=100, x=1.0, y=2.0))
players.append(Player(hp=80, x=4.0, y=8.0))
```

This should infer `list[Player]`. The empty list starts as an unresolved
`list[?T]`, and later local uses add constraints until `T` is known.

Conflicting constraints are errors, not silent heterogeneous containers:

```python
items = []
items.append(1)
items.append("hello")  # error: list element type already constrained to i32
```

If mixed values are intentional, use an explicit sum type or variant-like type:

```python
enum Item:
    Number:
        value: i32
    Text:
        value: str

items: list[Item] = []
items.append(Item.Number(value=1))
items.append(Item.Text(value="hello"))
```

Annotations are still required or strongly preferred for:

- function parameters
- function return types
- class fields
- constants
- uninitialized locals with no constraining later use
- fixed arrays
- raw pointers, references, and native handles when the initializer is `None`
- public ABI boundaries
- places where the programmer wants a narrower or wider explicit type

Examples:

```python
window: *struct SDL_Window = None
pixels: Color[PIXELS]
SAMPLE_RATE: i32 = 48000

def update(player: &Player, dt: f32):
    speed = 240.0
    player.x += speed * dt
```

The goal is aggressive-enough local inference for pleasant code, not global
guessing. If inference is ambiguous, Dudu should ask for a type annotation.

Inference should not cross arbitrary function side effects. This is valid when
`take_players` has a known parameter type:

```python
players = []
take_players(players)  # constrains players to list[Player] if the signature says so
```

This should remain ambiguous unless `load_items` has an explicit signature that
constrains the argument:

```python
items = []
load_items(items)
```

## Strictness

Dudu should feel Rust-like about correctness while still interoperating with
C/C++.

Default behavior:

- Dudu semantic errors are strict.
- Dudu semantic warnings should be visible by default once the warning is
  reliable.
- Dudu-generated C++ should be warning-clean under normal strict flags.
- User-provided C/C++ sources and system headers should not inherit Dudu's full
  warning policy automatically.
- `-Werror` should be opt-in for generated/native user builds.
- The Dudu compiler repository can use `-Werror` in developer scripts.

This keeps generated code clean without making package builds fail because a
different compiler or imported native header emits a warning.

## Packages And CLI

The compiler-driver command is `duc`. It stays explicit and file-oriented.

```sh
duc emit src/main.dd -o build/main.cpp
duc run src/main.dd
duc fmt
duc check src/main.dd
```

The project-driver command is `dudu`. It reads `dudu.toml` and coordinates
normal project actions without hiding the native toolchain.

```sh
dudu check
dudu build
dudu run
dudu test
dudu clean
```

Package metadata and native build settings can live in `dudu.toml`.

```toml
name = "demo"
entry = "src/main.dd"

[cxx]
standard = "c++20"

[include]
paths = ["include"]

[pkg]
libs = ["raylib"]
```

## Systems Surface

Dudu targets systems programming directly. The language surface includes:

- raw pointers and references
- fixed-width integer and float types
- fixed arrays and dynamic containers
- manual allocation with `new/delete` and `malloc/free`
- packed and aligned class attributes
- `volatile[T]`
- `atomic[T]`
- compile-time layout operators: `sizeof[T]`, `alignof[T]`, `offsetof[T](field)`
- function attributes such as `@inline`, `@extern_c`, and `@section(".name")`
- target attributes such as `@cuda.global` and `@shader.compute`
- address spaces such as `device[T]`, `storage[T]`, and `shared[T]`
- native C++ escape hatches through `cpp(...)`
- build modes for hosted, freestanding, embedded, CUDA, and shader-style targets

These features are part of the intended capability set, not Python
compatibility features.
