<a id="generics-and-value-parameters"></a>
# Generics And Value Parameters

[Dudu manual](https://dudulang.org/docs.html#generics) | Previous: [Compile-time programming](compile-time-programming.md) | Next: [Import semantics](import_semantics.md)

Dudu generics are compile-time C++ templates with Python-shaped declarations
and calls. They support Dudu types, imported native types, fixed extents, and
symbolic extent arithmetic.

## Generic Functions

Put generic parameters after the function name:

```python
def choose[T](value: T, fallback: T) -> T:
    if value < fallback:
        return fallback
    return value
```

The compiler infers `T` when arguments determine it:

```python
answer = choose(42, 0)          # i32
scale = choose(0.5, 1.0)       # f64
```

Supply arguments explicitly when inference is impossible or when a particular
native width is required:

```python
scale = choose[f32](0.5, 1.0)
```

Generic parameters can occur inside another type:

```python
def first[T](items: &const[list[T]]) -> T:
    return items[0]


names: list[str] = ["ada", "grace"]
name = first(names)             # str
```

Inference recursively matches the declared parameter type against the argument
type. Conflicting bindings are rejected:

```python
choose(1, "one")
```

```text
conflicting inferred type argument T: i32 vs str for choose
```

## Generic Classes

Generic classes use the same parameter list:

```python
class Box[T]:
    value: T

    def get(self) -> T:
        return self.value

    def set(self, value: T):
        self.value = value
```

Constructors state the class arguments explicitly:

```python
count: Box[i32] = Box[i32](42)
name: Box[str] = Box[str]("ada")
```

Multiple type parameters are positional:

```python
class Pair[Left, Right]:
    left: Left
    right: Right


entry = Pair[str, i32]("score", 42)
```

Class parameters are visible in fields, methods, nested generic types, and
operator declarations:

```python
class Vec2[T]:
    x: T
    y: T

    @operator("+")
    def add(self, other: &const[Vec2[T]]) -> Vec2[T]:
        return Vec2[T](self.x + other.x, self.y + other.y)
```

## Generic Methods

A method can introduce parameters in addition to its class parameters:

```python
class Converter[Input]:
    value: Input

    def convert[Output](self, fn: fn(Input) -> Output) -> Output:
        return fn(self.value)
```

Method arguments can be explicit or inferred exactly like free-function
arguments. Expected assignment and return types can also supply a method type
argument when the runtime arguments cannot:

```python
class Factory:
    def make[T](self) -> T:
        return T()


factory = Factory()
count: i32 = factory.make()
```

## Requirements On Generic Operations

Dudu does not use a separate `where` or trait-bound clause for ordinary
generics. The operations written in the generic body are the requirements.

```python
def add[T](left: T, right: T) -> T:
    return left + right
```

`add[i32]` is valid because `i32 + i32` is valid. Instantiating this function
with a type that has no `+` operator is a Dudu error. The diagnostic identifies
the operation and the concrete instantiation rather than deferring the failure
to template output from the C++ compiler.

The same rule applies to method calls, indexing, construction, comparison,
iteration, and user-defined `@operator` methods. Abstract classes remain the
nominal contract mechanism when an API needs an explicit interface rather than
use-site requirements.

## Type Parameters And Value Parameters

A generic parameter used as a type is a type parameter:

```python
class Box[T]:
    value: T
```

A parameter used in an array extent or shaped type is a compile-time `usize`
value parameter:

```python
class SmallBuffer[T, N]:
    items: array[T][N]
    count: usize
```

`T` lowers to a C++ type template parameter. `N` lowers to a C++ `size_t`
non-type template parameter:

```cpp
template <typename T, size_t N>
struct SmallBuffer {
    std::array<T, N> items;
    size_t count;
};
```

Value parameters are available as ordinary compile-time `usize` expressions
inside the body:

```python
def clear[T, N](values: &array[T][N]):
    for index: usize in range(N):
        values[index] = T()
```

Using a value parameter as a type is an error. Using a type parameter as a
runtime value is also an error.

## Inferring Value Parameters

The compiler infers value parameters from shaped arguments:

```python
def copy_matrix[T, Rows, Cols](
    source: &const[array[T][Rows, Cols]],
) -> array[T][Rows, Cols]:
    result: array[T][Rows, Cols]
    return result


source: array[f32][4, 8]
copy = copy_matrix(source)      # array[f32][4, 8]
```

The caller does not repeat `f32`, `4`, or `8`. Explicit arguments remain
available:

```python
copy = copy_matrix[f32, 4, 8](source)
```

Some equations cannot be uniquely inverted. Given only an argument shaped
`array[T][A * B, C]`, a compiler cannot determine one unique pair `A, B` from
the product. Supply those values explicitly or expose independently inferable
dimensions in the API.

## Value Arithmetic

Shape and value expressions support:

- integer literals
- value parameter names
- `+`, `-`, `*`, `/`, and `%`
- parentheses

Normal precedence applies. Division is compile-time integer division because
value parameters are `usize`.

```python
def grouped_count[N](
    values: &array[i32][N],
) -> array[i32][N / 4 + N % 4]:
    groups: array[i32][N / 4 + N % 4]
    return groups


values: array[i32][10]
groups = grouped_count(values)  # array[i32][4]
```

Substitution folds concrete results. Symbolic results remain normalized
expressions:

```python
def flatten[T, Batch, Channels, Height, Width](
    value: Tensor[T][Batch, Channels, Height, Width],
) -> Tensor[T][Batch, Channels * Height * Width]:
    return assume_shape[Tensor[T][Batch, Channels * Height * Width]](value)
```

Normalization preserves operator order and parentheses needed by precedence.
It does not assume arbitrary algebraic identities. Write equivalent public
shapes in one canonical order rather than relying on commutative rearrangement.

Unsupported operators are rejected as malformed type syntax:

```python
value: Tensor[f32][Height ** 2]
```

## Compile-Time And Runtime Shapes

`dyn` means an extent is known at runtime:

```python
batch: Tensor[f32][dyn, 784]
```

It may flow through an unchanged `dyn` dimension, but it cannot be used in
compile-time arithmetic:

```python
result: Tensor[f32][dyn * 3]
```

```text
shape expression uses runtime dyn in compile-time arithmetic: dyn * 3
```

Static extents are optional contracts. Runtime-shaped numeric libraries can
use `Tensor[T][dyn, dyn]`; APIs that benefit from compile-time plumbing checks
can expose concrete or symbolic extents.

`assume_shape[Target](value)` narrows runtime metadata only after user or
library code has validated it. It does not reshape, allocate, or copy data.

## Matrix And Convolution Example

```python
def conv2d[Height, Width, Kernel](
    image: &array[f32][Height, Width],
    kernel: &array[f32][Kernel, Kernel],
) -> array[f32][Height - Kernel + 1, Width - Kernel + 1]:
    out: array[f32][Height - Kernel + 1, Width - Kernel + 1]
    out_h = Height - Kernel + 1
    out_w = Width - Kernel + 1

    for y: usize in range(out_h):
        for x: usize in range(out_w):
            acc: f32 = 0.0
            for ky: usize in range(Kernel):
                for kx: usize in range(Kernel):
                    acc += image[y + ky, x + kx] * kernel[ky, kx]
            out[y, x] = acc
    return out


image: array[f32][32, 32]
kernel: array[f32][3, 3]
output = conv2d(image, kernel)  # array[f32][30, 30]
```

`Height`, `Width`, and `Kernel` are inferred from the two arguments. They are
also typed as `usize` inside the function. The result type and editor inlay hint
contain the folded output shape.

## Imported C++ Templates

Native templates keep their C++ namespace and use Dudu brackets for explicit
arguments:

```python
from cpp import vector
from cpp import memory


values: std.vector[i32]
owner = std.make_unique[Widget](42)
```

Default C++ template arguments, overloaded template functions, member
templates, dependent return types, and non-type template arguments are read
from Clang header metadata. Dudu does not encode individual standard-library
templates in the compiler.

Use a local or third-party template through a path import:

```python
from cpp.path import vendor/small_vector.hpp


values: vendor.small_vector[f32, 16]
```

Native import forms and macro behavior are documented in
`docs/native-imports.md`.

## Generated C++ Interface

Dudu emits generic declarations as C++ templates. Generated module headers
retain templates so C++ callers can instantiate Dudu generic functions and
classes directly. Generic Dudu modules therefore do not require a closed set of
pre-generated specializations.

## Diagnostics And Editor Information

Dudu diagnoses:

- duplicate generic parameters
- too few or too many explicit arguments
- parameters that cannot be inferred
- conflicting inferred arguments
- type parameters used as values
- value parameters used as types
- unsupported operations at a concrete instantiation
- malformed value expressions
- `dyn` used in compile-time arithmetic
- shaped argument and result mismatches

Hover shows declared generic signatures. Inlay hints show inferred local types,
including folded result extents such as `array[i32][4]`. Value parameters used
inside a body are shown as `usize`.

## Limits

- Dudu generics lower to C++ templates; they are not runtime-erased Python
  generics or unrestricted dependent types.
- A type parameter cannot be used as a value, and a value parameter cannot be
  used as a type.
- `dyn` marks a runtime extent and cannot participate in required compile-time
  arithmetic.
- Imported C++ template behavior is limited to declarations Clang can expose
  and Dudu can represent at the native boundary.

## Tested Examples

- [`generics_reference.dd`](../tests/fixtures/generics_reference.dd) executes
  inferred and explicit types, generic classes, value parameters, and extent
  arithmetic.
- [`generic_substitution_shapes.dd`](../tests/fixtures/generic_substitution_shapes.dd)
  checks shaped substitution across calls.
- [`bad_generic_inferred_conflict.dd`](../tests/fixtures/bad_generic_inferred_conflict.dd)
  and [`bad_generic_value_param_as_type.dd`](../tests/fixtures/bad_generic_value_param_as_type.dd)
  verify inference and kind diagnostics.
