# Dudu Appearance Spec

This is the current surface-language sketch for Dudu. It describes how code
should look before the compiler catches up with every form.

The main idea:

```text
Dudu is expression trees without nested parentheses.
Flat arguments use spaces. Nested arguments use indentation.
```

## Files

Dudu source files use `.dd`.

```text
main.dd
math.dd
renderer.dd
```

## Comments

Line comments start with `#`.

```dudu
# This is a comment.
```

## Imports

Paths stay quoted because they are external filenames or headers, not Dudu
identifiers.

```dudu
use "math.dd"
use "renderer/camera.dd"
```

C and C++ imports name a header and an alias:

```dudu
use c "stdio.h" as c
use cpp "raylib.h" as rl
use cpp "glm/glm.hpp" as glm
```

Native package imports can be added later:

```dudu
use math
use glm as glm
```

## Expressions

A line is an expression or statement. Words after the head are flat arguments:

```dudu
circle x y radius color
```

The same call can be written as an indented tree:

```dudu
circle
    x
    y
    radius
    color
```

Nested expressions use more indentation:

```dudu
circle
    add x 10
    y
    radius
    rgb 1 0 0
```

Meaning:

```cpp
circle(add(x, 10), y, radius, rgb(1, 0, 0));
```

Flat calls are for simple arguments. Indentation is the normal way to express
nested calls.

## Assignment

Assignment and initialization use `=`.

```dudu
x = 10
name = "dudu"
```

Equality comparison remains `==`.

```dudu
if x == 10
    print "ten"
```

`=` means bind, assign, or store. It does not mean equality.

Compound assignment uses familiar C-family spellings:

```dudu
count += 1
count -= 1
total += value
```

## Built-In Types

Scalar types use Rust-like spellings:

```dudu
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

Likely C++ lowerings:

```text
str  -> std::string
cstr -> const char*
```

`void` exists as an interop/result type, but ordinary Dudu functions should
omit the return type when they return nothing.

## Locals

Mutable locals do not use `var` or `let`.

```dudu
count i32 = 0
name str = "dudu"
player Player
```

Type inference is allowed when the initializer is enough:

```dudu
count = 10
scale = 2.0
```

Fixed bindings use `uni`. A `uni` binding cannot be rebound or reassigned.

```dudu
uni max_count i32 = 128
uni tau f32 = 6.283185307
uni title = "demo"
```

Top-level fixed bindings use the same form:

```dudu
uni default_width i32 = 800
```

`con` is reserved for const-qualified types, especially around pointers and
references.

## Functions

The return type goes on the function line.

```dudu
fn add i32
    a i32
    b i32

    a + b
```

Functions with no return type return nothing:

```dudu
fn log
    msg cstr

    c.printf "msg: %s\n" msg
```

The last expression is the return value for non-void functions.

```dudu
fn square i32
    x i32

    x * x
```

`ret` is for early return.

```dudu
fn clamp i32
    x i32
    lo i32
    hi i32

    if x < lo
        ret lo

    if x > hi
        ret hi

    x
```

Function arguments live under the function header. Complex argument types can
continue on deeper indentation.

```dudu
fn lookup i32
    cache map
        str
        i32
    key str

    cache[key]
```

## Things

`th` declares a thing. A thing lowers to a C++ `struct` by default.

```dudu
th Vec2
    x f32
    y f32
```

Things can contain fields and methods.

```dudu
th Player
    pos Vec2
    hp i32

    fn damage
        amount i32

        hp = hp - amount

    fn alive bool
        hp > 0
```

Methods can also be defined out of line.

```dudu
fn Player.damage
    amount i32

    hp = hp - amount
```

Inside a method, fields can be referenced directly. `self` can exist later for
cases where explicit receiver access is clearer.

## Enums

Enums can omit their backing type:

```dudu
enum Direction
    north
    south
    east
    west
```

Or specify one:

```dudu
enum Color u8
    red 1
    green 2
    blue 3
```

Enums lower to scoped C++ enums unless an interop rule says otherwise.

## Type Aliases

`tp` declares a type alias.

```dudu
tp PlayerId u64
```

Complex aliases can use indentation:

```dudu
tp ScoreMap map
    str
    i32
```

Type aliases are the preferred way to keep function signatures readable when a
return type is a large tree.

## Multiple Return Values

Dudu does not have built-in multiple return values in v1. Return a thing when a
function produces multiple named values.

```dudu
th MinMax
    lo i32
    hi i32

fn minmax MinMax
    values [i32]

    lo i32 = values[0]
    hi i32 = values[0]

    for v in values
        if v < lo
            lo = v
        if v > hi
            hi = v

    MinMax lo hi
```

Callers use the named fields:

```dudu
mm = minmax values
print mm.lo mm.hi
```

## Type Trees

Type constructors use the same flat-or-indented shape as calls.

Flat:

```dudu
scores map str i32
seen set str
```

Indented:

```dudu
scores map
    str
    i32

seen set
    str
```

Deep nesting is written as a tree.

```dudu
cache map
    str
    map
        i32
        set
            f32
```

Meaning:

```cpp
std::unordered_map<
    std::string,
    std::unordered_map<int32_t, std::unordered_set<float>>
> cache;
```

## Arrays

Dynamic arrays use bracket type syntax.

```dudu
nums [i32]
```

Fixed arrays include the count.

```dudu
tiles [Tile 256]
```

Nested arrays:

```dudu
grid [[f32]]
```

Indexing uses normal brackets:

```dudu
x = nums[3]
cell = grid[y][x]
nums[3] = 10
```

Likely C++ lowerings:

```text
[T]   -> std::vector<T>
[T N] -> std::array<T, N>
```

For very complex element types, use a type alias or an indented type tree with a
container type such as `std.array`.

## Maps And Sets

Dudu has convenience map and set type constructors.

```dudu
scores map str i32
seen set str
```

Likely C++ lowerings:

```text
map K V -> std::unordered_map<K, V>
set T   -> std::unordered_set<T>
```

Explicit C++ container names are still allowed:

```dudu
ordered std.map
    str
    i32

fast std.unordered_map
    str
    i32
```

## C++ Templates

Imported C++ template types use the same type tree form.

```dudu
points std.vector
    glm.vec
        3
        f32
```

Meaning:

```cpp
std::vector<glm::vec<3, float>> points;
```

Flat form is allowed when readable:

```dudu
ids std.vector i32
```

Template calls need one explicit final rule because C++ separates template args
from call args. The current practical escape hatch is bracketed template args:

```dudu
ptr = std.make_unique[Thing] 10 20
```

This can be replaced by a cleaner Dudu-native spelling later.

## Pointers And References

Dudu uses familiar C-family address operators, but keeps the type syntax prefix
and unambiguous.

```dudu
*T    # pointer to T
&T    # reference to T
&x    # address of x
*p    # value at pointer p
```

Canonical type spelling sticks the symbol to the type:

```dudu
p *i32
r &i32
```

This avoids the C declaration trap. The modifier belongs to the type, not to a
particular variable name.

```dudu
a *i32
b i32
```

means:

```cpp
int32_t* a;
int32_t b;
```

Address-of:

```dudu
value i32 = 42
p *i32 = &value
```

Dereference for read and write:

```dudu
copy i32 = *p
*p = 99
```

Reference binding:

```dudu
value i32 = 42
r &i32 = value
r += 1
```

Pointer or reference to const:

```dudu
p *con i32
r &con i32
```

Fixed pointer binding:

```dudu
uni p *i32 = &value
```

So the core forms are:

```text
*T     -> pointer type
&T     -> reference type
&x     -> address of x
*p     -> value at pointer p
con T  -> const-qualified T
uni x  -> fixed binding
```

## Member And Namespace Access

Dudu source uses dot for member, method, and namespace paths.

```dudu
player.pos.x
std.vector
glm.vec
rl.InitWindow
```

The emitter lowers based on context:

```text
object.field   -> object.field
pointer.field  -> pointer->field
namespace.name -> namespace::name
```

## Conditionals

Statement form:

```dudu
if hp <= 0
    dead = true
else
    hp = hp - 1
```

Expression-valued form:

```dudu
status = if hp <= 0
    "dead"
else
    "alive"
```

The last expression in each branch is the branch value.

## Loops

While loop:

```dudu
while i < count
    sum = sum + values[i]
    i = i + 1
```

Range loop:

```dudu
for i in 0..count
    sum = sum + values[i]
```

C++-style range loop:

```dudu
for item in items
    print item
```

The exact lowering rules for ranges are still open.

## Constructors

Simple construction uses the type name as a call-like expression.

```dudu
pos Vec2 = Vec2 10 20
```

Multiline:

```dudu
pos Vec2 = Vec2
    10
    20
```

This should lower to aggregate or constructor initialization depending on the
target type.

Named field construction is not decided yet.

## C Interop

```dudu
use c "stdio.h" as c

fn main i32
    c.printf "hello %s\n" "dudu"
    0
```

C structs, enums, functions, and constants come through generated C++ includes
first. The C++ compiler validates the final call shapes until Dudu has a real
header importer.

## C++ Interop

```dudu
use cpp "raylib.h" as rl

fn main i32
    rl.InitWindow 800 450 "Dudu"

    while not rl.WindowShouldClose
        rl.BeginDrawing
        rl.ClearBackground rl.BLACK
        rl.DrawText "hello" 20 20 20 rl.WHITE
        rl.EndDrawing

    rl.CloseWindow
    0
```

GLM-style math:

```dudu
use cpp "glm/glm.hpp" as glm

fn main i32
    a glm.vec
        3
        f32
    b glm.vec
        3
        f32

    d f32 = glm.dot a b
    0
```

Operator overloads from imported C++ types should work naturally where the
generated C++ supports them.

## Packages And CLI

The compiler command is planned as `duc`.

```sh
duc emit src/main.dd -o build/main.cpp
duc build
duc run
duc test
```

Package metadata can live in `dudu.toml`.

```toml
name = "demo"
main = "src/main.dd"
cpp_std = "c++20"

[cc]
include_dirs = ["include"]
libs = ["raylib"]
```

## Open Decisions

- Whether template function calls need bracketed template args or a better tree
  form.
- Exact range syntax and inclusive/exclusive range spellings.
- Named field construction.
- How much generated header/source splitting Dudu should expose in v1.
