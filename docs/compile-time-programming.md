<a id="compile-time-programming"></a>
# Compile-Time Programming

[Dudu manual](https://dudulang.org/docs.html#generics) | Previous: [Fixed arrays and numeric literals](fixed-arrays-and-numeric-literals.md) | Next: [Generics and value parameters](generics.md)

Dudu uses ordinary typed expressions for constants, layout checks, fixed-array
extents, value-generic arguments, and build configuration. It lowers these
forms to C++20 constant expressions rather than providing a separate compile-
time language.

## Constants

An all-caps binding is immutable:

```python
WIDTH: i32 = 320
HEIGHT: i32 = 240
PIXELS: i32 = WIDTH * HEIGHT
```

When the initializer is a constant expression, a module constant lowers to
C++ `constexpr` and can be used wherever a compile-time value is required.
All-caps naming alone does not make a runtime operation constant-evaluable.

Use snake-case for a module binding that is not intended to read as a
compile-time constant:

```python
active_backend = load_backend()
```

The editor marks all-caps constants as readonly. Hover reports the declared or
inferred type.

## Constant Contexts

Some Dudu forms require a value that the compiler and generated C++ can know at
compile time:

- fixed-array extents such as `array[u8][PACKET_BYTES]`
- value-generic arguments such as `SmallBuffer[u8, CAPACITY]`
- `static_assert` conditions
- `sizeof`, `alignof`, and `offsetof` results used by another constant context

Normal local variables and function bodies remain runtime code unless they are
used through one of these forms.

## Compile-Time Functions

Mark a function `@constexpr` when callers may evaluate it at compile time:

```python
@constexpr
def align_up(value: usize, alignment: usize) -> usize:
    return (value + alignment - 1) & ~(alignment - 1)


PACKET_BYTES: usize = align_up(1500, 64)
```

The generated C++ function is `constexpr`. The same Dudu function can also be
called with runtime values; `@constexpr` permits compile-time evaluation but
does not force every call to happen at compile time.

A Dudu `@constexpr` function may call another Dudu function only when that
function is also marked `@constexpr`:

```python
def read_runtime_setting() -> usize:
    return 64


@constexpr
def capacity() -> usize:
    return read_runtime_setting()
```

The compiler reports:

```text
compile-time expression calls non-constexpr function: read_runtime_setting
```

Imported native calls are governed by their C++ constant-expression rules. If
a native function cannot be evaluated in a required constant context, the
native compiler diagnostic is reported during the C++ build.

## Static Assertions

`static_assert` rejects an invalid program while compiling:

```python
MAX_CONNECTIONS: usize = 1024
static_assert(MAX_CONNECTIONS % 64 == 0)
```

Layout checks use the same form:

```python
@pack(1)
class PacketHeader:
    kind: u16
    flags: u16
    payload_size: u32


static_assert(sizeof[PacketHeader]() == 8)
static_assert(alignof[PacketHeader]() == 1)
static_assert(offsetof[PacketHeader](payload_size) == 4)
```

A simple assertion that Dudu can evaluate directly produces a Dudu diagnostic:

```python
WIDTH: i32 = 8
HEIGHT: i32 = 8
static_assert(WIDTH * HEIGHT == 65)
```

```text
static_assert failed: WIDTH * HEIGHT == 65
```

The generated C++ retains `static_assert`, so the C++ compiler also validates
expressions that depend on full native C++ constant-evaluation semantics.

## Fixed Capacity And Lookup Tables

Constants can define storage without repeating literal sizes:

```python
QUEUE_CAPACITY: usize = 64
LUT_BITS: usize = 8
LUT_SIZE: usize = 1 << LUT_BITS

queue: array[u32][QUEUE_CAPACITY]
gamma_lut: array[u8][LUT_SIZE]

static_assert(LUT_SIZE == 256)
```

These are fixed native arrays. The extents are part of their types and no
runtime allocation is introduced.

## Value-Generic Arithmetic

Compile-time values can be parameters of generic functions and classes:

```python
class SmallBuffer[T, N]:
    items: array[T][N]


def valid_window[Input, Kernel](
    input: &array[u8][Input],
    kernel: &array[u8][Kernel],
) -> array[u8][Input - Kernel + 1]:
    output: array[u8][Input - Kernel + 1]
    return output
```

Shaped parameters can supply value arguments without repeating them at the
call site:

```python
def conv2d[H, W, K](
    image: &array[f32][H, W],
    kernel: &array[f32][K, K],
) -> array[f32][H - K + 1, W - K + 1]:
    result: array[f32][H - K + 1, W - K + 1]
    return result


image: array[f32][32, 32]
kernel: array[f32][3, 3]
output = conv2d(image, kernel)
```

Here `H`, `W`, and `K` are compile-time `usize` values inferred from the
arguments. The return shape is `array[f32][30, 30]`. Runtime-shaped dimensions
use `dyn` instead and cannot participate in compile-time extent arithmetic.

The complete generic rules are documented in `docs/generics.md`.

## Build Values

Project build values are declared in `dudu.toml`:

```toml
[build]
DEBUG = true
RENDER_BACKEND = "vulkan"
MAX_LIGHTS = 128
```

Source code reads them through the `build` namespace:

```python
if build.DEBUG:
    enable_validation()

if build.RENDER_BACKEND == "vulkan":
    init_vulkan()
elif build.RENDER_BACKEND == "raylib":
    init_raylib()
```

A condition composed only from `build.*` values is emitted as C++
`if constexpr`. The unselected branch does not execute and can be removed by
the native compiler.

Command-line definitions override or add values for one invocation:

```sh
dudu build -DDEBUG=false -DRENDER_BACKEND='"raylib"' -DMAX_LIGHTS=64
```

The quotes around a command-line string are part of the value passed to the
compiler. Boolean and numeric values do not need embedded quotes.

Dudu always defines these target values:

- `build.TARGET_KIND`: `"executable"`, `"static"`, or another configured
  target kind
- `build.TARGET_MODE`: `"hosted"`, `"freestanding"`, or `"embedded"`

An unknown build name is an error at its source location:

```text
unknown build flag: build.NOPE
```

## Evaluation Boundary

The following are compile-time when their inputs are compile-time values:

- arithmetic, comparison, bitwise, and boolean expressions
- all-caps constants initialized from constant expressions
- calls to Dudu `@constexpr` functions
- C++ operations accepted as constant expressions by the native compiler
- `sizeof[T]()`, `alignof[T]()`, and `offsetof[T](field)`

The following are runtime:

- mutable local and module bindings
- calls to ordinary Dudu functions
- allocation, I/O, clocks, input devices, and other runtime state
- dimensions written as `dyn`

When runtime data reaches a constant context, Dudu reports the error when it
has enough semantic information. Native constant-expression failures are
reported by the C++ compiler with the generated source location included in
the build output.

## Limits

- `@constexpr` follows the C++20 constant-expression model; it is not a second
  interpreter or unrestricted compile-time language.
- Runtime I/O, allocation, mutable state, and ordinary function calls cannot
  enter a required constant context.
- Build values are explicit manifest or `-D` inputs, not ambient environment
  variables.
- Native C++ expressions still have to satisfy the imported compiler's
  constant-expression rules.

## Tested Examples

- [`compile_time_programming.dd`](../tests/fixtures/compile_time_programming.dd)
  executes constants, `@constexpr`, layout operations, build selection, and
  value arithmetic.
- [`bad_constexpr_body_calls_runtime.dd`](../tests/fixtures/bad_constexpr_body_calls_runtime.dd)
  verifies the runtime-call boundary.
- [`bad_static_assert.dd`](../tests/fixtures/bad_static_assert.dd) and
  [`bad_build_flag.dd`](../tests/fixtures/bad_build_flag.dd) verify compile-time
  diagnostics.
